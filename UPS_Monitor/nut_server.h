#pragma once
#include <AsyncTCP.h>
#include "config.h"
#include "ina3221.h"

// Кеш показань, що заповнюється в loop() головного файлу.
extern Reading cachedReadings[3];

// ---------- Допоміжні функції ----------

int findChannelByLabel(const char* label) {
  for (int i = 0; i < 3; i++) {
    if (strcmp(CH_CAL[i].label, label) == 0) return i;
  }
  return -1; // канал з такою міткою не налаштований - обробники нижче це враховують
}

float estimateBatteryChargePercent(float voltage) {
  // ПРИМІТКА: розряджувальна крива LiFePO4 дуже плоска, тож оцінка заряду
  // за самою напругою — груба (особливо в середині діапазону). Точніше
  // рахувати coulomb counting (інтегрування виміряного струму в часі) —
  // можемо додати пізніше, коли знадобиться реальна точність % заряду.
  if (voltage <= BATTERY_EMPTY_V) return 0.0f;
  if (voltage >= BATTERY_FULL_V) return 100.0f;
  return (voltage - BATTERY_EMPTY_V) / (BATTERY_FULL_V - BATTERY_EMPTY_V) * 100.0f;
}

String buildUpsStatus() {
  int psuIdx  = findChannelByLabel("PSU in");
  int battIdx = findChannelByLabel("Battery");

  bool online  = (psuIdx >= 0) && (cachedReadings[psuIdx].busVoltage_V > INPUT_PRESENT_V);
  bool lowBatt = (battIdx >= 0) && (cachedReadings[battIdx].busVoltage_V > 0.5f) &&
                 (cachedReadings[battIdx].busVoltage_V < BATTERY_LOW_V);

  String status = online ? "OL" : "OB";
  if (lowBatt) status += " LB";
  return status;
}

// Повертає значення змінної NUT за назвою. false, якщо змінна невідома
// (наприклад канал з відповідною міткою не налаштований у CH_CAL).
bool getNutVarValue(const String& name, String& out) {
  int psuIdx  = findChannelByLabel("PSU in");
  int battIdx = findChannelByLabel("Battery");
  int outIdx  = findChannelByLabel("UPS out");

  if (name == "device.type")               { out = "ups"; return true; }
  if (name == "device.mfr" || name == "ups.mfr") { out = "DIY"; return true; }
  if (name == "ups.model")                 { out = "ESP32-S3 UPS Monitor"; return true; }
  if (name == "ups.firmware")              { out = FW_VERSION; return true; }
  if (name == "ups.status")                { out = buildUpsStatus(); return true; }

  if (battIdx >= 0) {
    if (name == "battery.voltage") { out = String(cachedReadings[battIdx].busVoltage_V, 2); return true; }
    if (name == "battery.current") { out = String(cachedReadings[battIdx].current_mA / 1000.0f, 3); return true; }
    if (name == "battery.charge")  { out = String(estimateBatteryChargePercent(cachedReadings[battIdx].busVoltage_V), 0); return true; }
  }
  if (psuIdx >= 0) {
    if (name == "input.voltage") { out = String(cachedReadings[psuIdx].busVoltage_V, 2); return true; }
  }
  if (outIdx >= 0) {
    if (name == "output.voltage") { out = String(cachedReadings[outIdx].busVoltage_V, 2); return true; }
    if (name == "output.current") { out = String(cachedReadings[outIdx].current_mA / 1000.0f, 3); return true; }
    if (name == "ups.power" || name == "ups.realpower") { out = String(cachedReadings[outIdx].power_mW / 1000.0f, 1); return true; }
    if (name == "ups.load") {
      float loadPct = (cachedReadings[outIdx].power_mW / 1000.0f) / UPS_RATED_POWER_W * 100.0f;
      if (loadPct < 0) loadPct = 0;
      out = String(loadPct, 0);
      return true;
    }
  }
  return false;
}

// Повний перелік змінних, які віддає LIST VAR (лише ті, для яких getNutVarValue поверне true)
const char* NUT_VAR_NAMES[] = {
  "device.type", "device.mfr", "ups.mfr", "ups.model", "ups.firmware", "ups.status",
  "battery.voltage", "battery.current", "battery.charge",
  "input.voltage",
  "output.voltage", "output.current", "ups.power", "ups.realpower", "ups.load"
};
const int NUT_VAR_COUNT = sizeof(NUT_VAR_NAMES) / sizeof(NUT_VAR_NAMES[0]);

// ---------- Контекст клієнта та сам сервер ----------

struct NutClientCtx {
  String buffer;     // накопичення байтів до символу переносу рядка
  String username;   // збережене значення з USERNAME (для довідки, не критично)
  bool loggedIn = false;
};

AsyncServer nutServer(NUT_PORT);

void nutSend(AsyncClient* client, const String& line) {
  if (client && client->connected()) {
    String out = line + "\n";
    client->write(out.c_str(), out.length());
  }
}

void handleNutLine(AsyncClient* client, NutClientCtx* ctx, String line) {
  line.trim();
  if (line.length() == 0) return;

  // Простий токенізатор без підтримки лапок з пробілами всередині -
  // для наших команд (upsname/varname без пробілів) цього достатньо.
  int sp1 = line.indexOf(' ');
  String cmd  = (sp1 == -1) ? line : line.substring(0, sp1);
  String rest = (sp1 == -1) ? ""   : line.substring(sp1 + 1);
  cmd.toUpperCase();

  if (cmd == "USERNAME") {
    ctx->username = rest;
    nutSend(client, "OK");
    return;
  }

  if (cmd == "PASSWORD") {
    if (rest == NUT_PASS) {
      ctx->loggedIn = true;
      nutSend(client, "OK");
    } else {
      nutSend(client, "ERR ACCESS-DENIED");
    }
    return;
  }

  if (cmd == "LOGIN") {
    if (rest != NUT_UPS_NAME) { nutSend(client, "ERR UNKNOWN-UPS"); return; }
    nutSend(client, "OK");
    return;
  }

  if (cmd == "LOGOUT") {
    nutSend(client, "OK");
    client->close();
    return;
  }

  if (cmd == "PRIMARY" || cmd == "MASTER") { // MASTER - стара назва цієї ж команди в NUT
    if (rest != NUT_UPS_NAME) { nutSend(client, "ERR UNKNOWN-UPS"); return; }
    nutSend(client, "OK");
    return;
  }

  if (cmd == "STARTTLS") {
    // TLS не підтримуємо - клієнт має відкотитись на звичайне з'єднання
    nutSend(client, "ERR FEATURE-NOT-SUPPORTED");
    return;
  }

  if (cmd == "VER") {
    nutSend(client, String("Network UPS Tools upsd ") + FW_VERSION + " (ESP32-S3 DIY)");
    return;
  }

  if (cmd == "NETVER") {
    nutSend(client, "1.2");
    return;
  }

  if (cmd == "LIST") {
    int sp2 = rest.indexOf(' ');
    String sub = (sp2 == -1) ? rest : rest.substring(0, sp2);
    String arg = (sp2 == -1) ? ""   : rest.substring(sp2 + 1);
    sub.toUpperCase();

    if (sub == "UPS") {
      nutSend(client, "BEGIN LIST UPS");
      nutSend(client, String("UPS ") + NUT_UPS_NAME + " \"" + NUT_UPS_DESC + "\"");
      nutSend(client, "END LIST UPS");
      return;
    }

    // Усі інші LIST-підкоманди вимагають ім'я UPS як аргумент
    String upsname = arg;
    if (upsname != NUT_UPS_NAME) { nutSend(client, "ERR UNKNOWN-UPS"); return; }

    if (sub == "VAR") {
      nutSend(client, String("BEGIN LIST VAR ") + upsname);
      for (int i = 0; i < NUT_VAR_COUNT; i++) {
        String val;
        if (getNutVarValue(NUT_VAR_NAMES[i], val)) {
          nutSend(client, String("VAR ") + upsname + " " + NUT_VAR_NAMES[i] + " \"" + val + "\"");
        }
      }
      nutSend(client, String("END LIST VAR ") + upsname);
      return;
    }

    if (sub == "RW") {
      nutSend(client, String("BEGIN LIST RW ") + upsname);
      nutSend(client, String("END LIST RW ") + upsname); // поки без змінних, доступних для запису
      return;
    }

    if (sub == "CMD") {
      nutSend(client, String("BEGIN LIST CMD ") + upsname);
      nutSend(client, String("END LIST CMD ") + upsname); // поки без інстант-команд
      return;
    }

    if (sub == "CLIENT") {
      nutSend(client, String("BEGIN LIST CLIENT ") + upsname);
      nutSend(client, String("END LIST CLIENT ") + upsname);
      return;
    }

    nutSend(client, "ERR UNKNOWN-COMMAND");
    return;
  }

  if (cmd == "GET") {
    int sp2 = rest.indexOf(' ');
    String sub  = (sp2 == -1) ? rest : rest.substring(0, sp2);
    String tail = (sp2 == -1) ? ""   : rest.substring(sp2 + 1);
    sub.toUpperCase();

    int sp3 = tail.indexOf(' ');
    String upsname = (sp3 == -1) ? tail : tail.substring(0, sp3);
    String varname = (sp3 == -1) ? ""   : tail.substring(sp3 + 1);

    if (upsname != NUT_UPS_NAME) { nutSend(client, "ERR UNKNOWN-UPS"); return; }

    if (sub == "UPSDESC") {
      nutSend(client, String("UPSDESC ") + upsname + " \"" + NUT_UPS_DESC + "\"");
      return;
    }

    if (sub == "NUMLOGINS") {
      nutSend(client, String("NUMLOGINS ") + upsname + " 1");
      return;
    }

    if (sub == "VAR") {
      String val;
      if (getNutVarValue(varname, val)) {
        nutSend(client, String("VAR ") + upsname + " " + varname + " \"" + val + "\"");
      } else {
        nutSend(client, "ERR VAR-NOT-SUPPORTED");
      }
      return;
    }

    if (sub == "TYPE") {
      String val;
      if (getNutVarValue(varname, val)) {
        nutSend(client, String("TYPE ") + upsname + " " + varname + " STRING:64");
      } else {
        nutSend(client, "ERR VAR-NOT-SUPPORTED");
      }
      return;
    }

    if (sub == "DESC") {
      String val;
      if (getNutVarValue(varname, val)) {
        nutSend(client, String("DESC ") + upsname + " " + varname + " \"" + varname + "\"");
      } else {
        nutSend(client, "ERR VAR-NOT-SUPPORTED");
      }
      return;
    }

    nutSend(client, "ERR UNKNOWN-COMMAND");
    return;
  }

  nutSend(client, "ERR UNKNOWN-COMMAND");
}

void nutServerBegin() {
  nutServer.onClient([](void* arg, AsyncClient* client){
    NutClientCtx* ctx = new NutClientCtx();

    client->onData([](void* arg, AsyncClient* client, void* data, size_t len){
      NutClientCtx* ctx = (NutClientCtx*)arg;
      ctx->buffer.concat((const char*)data, len); // бінарно-безпечний append (без опори на null-термінатор)

      int nl;
      while ((nl = ctx->buffer.indexOf('\n')) != -1) {
        String line = ctx->buffer.substring(0, nl);
        ctx->buffer.remove(0, nl + 1);
        handleNutLine(client, ctx, line);
      }
    }, ctx);

    client->onDisconnect([](void* arg, AsyncClient* client){
      NutClientCtx* ctx = (NutClientCtx*)arg;
      delete ctx;
      delete client;
    }, ctx);

  }, nullptr);

  nutServer.begin();
  Serial.printf("NUT сервер запущено на порту %d (UPS name: %s)\n", NUT_PORT, NUT_UPS_NAME);
}
