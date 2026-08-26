#pragma once
#include <AsyncTCP.h>
#include <math.h>
#include <Preferences.h>
#include "config.h"
#include "ina3221.h"

// Кеш показань, що заповнюється в loop() головного файлу.
extern Reading cachedReadings[3];
extern Preferences prefs;

// Runtime-налаштування батареї (завантажуються з Preferences в setup(),
// редагуються через /settings/battery) - визначені в головному .ino файлі.
extern float batterySoc100V;
extern float batterySoc0V;
extern int   batteryCapacityMah;
extern float batterySocMah;   // поточний стан заряду (coulomb counter), мАг

// ---------- Допоміжні функції ----------

int findChannelByLabel(const char* label) {
  for (int i = 0; i < 3; i++) {
    if (strcmp(CH_CAL[i].label, label) == 0) return i;
  }
  return -1; // канал з такою міткою не налаштований - обробники нижче це враховують
}

// Груба оцінка SOC% лише за напругою. Більше НЕ використовується напряму
// для показу користувачу (для цього тепер служить batterySocPercent() -
// coulomb counting, точніший на плоскій ділянці кривої LiFePO4) - лишається
// тут для (а) стартового значення coulomb-каунтера на першому запуску,
// коли ще нема збереженого стану, і (б) виявлення прив'язок SOC100/SOC0
// нижче в updateCoulombCounter().
float estimateBatteryChargePercentByVoltage(float voltage) {
  if (voltage <= batterySoc0V) return 0.0f;
  if (voltage >= batterySoc100V) return 100.0f;
  return (voltage - batterySoc0V) / (batterySoc100V - batterySoc0V) * 100.0f;
}

// Компенсує просідання/підйом напруги під струмом (IR-падіння), щоб
// прив'язки нижче спрацьовували від напруги "спокою", а не від миттєвої
// напруги під навантаженням. current_mA зі знаком: + зарядка, - розряд
// (вже виправлено на рівні read() в ina3221.h через ChannelCal::currentSign).
float estimateOpenCircuitVoltage(float measuredV, float current_mA) {
  return measuredV - (current_mA / 1000.0f) * BATTERY_INTERNAL_RESISTANCE_OHM;
}

// Поточний SOC% - за coulomb-каунтером (інтегрування струму в часі, що
// самокоригується по напрузі на прив'язках SOC100/SOC0 - див.
// updateCoulombCounter() нижче).
float batterySocPercent() {
  if (batteryCapacityMah <= 0) return 0.0f;
  float pct = (batterySocMah / batteryCapacityMah) * 100.0f;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

// ---------- Coulomb counting ----------
// Інтегрує струм батарейного каналу в часі, самокоригуючись по напрузі на
// двох "прив'язках": коли (компенсована) напруга сягає batterySoc100V, поки
// струм не від'ємний (зарядка або вже майже нульовий струм у "хвості"
// заряду) - вважаємо батарею повною. Коли напруга спадає до batterySoc0V,
// поки струм не додатний - вважаємо батарею порожньою і заразом повільно
// підлаштовуємо збережену ємність (batteryCapacityMah) під реально
// спостережену ємність цього циклу розряду (враховує деградацію з часом).
//
// ОБМЕЖЕННЯ: mahDischargedSinceFull живе лише в RAM (не переживає
// перезавантаження) - якщо пристрій перезавантажиться десь посередині між
// прив'язками "повна" і "порожня", навчання ємності для цього циклу
// втратиться (сам стан заряду batterySocMah при цьому не постраждає - він
// зберігається окремо). Якщо потрібна стійкість і до цього - можемо
// додати збереження цього лічильника теж.
uint32_t lastCoulombMs = 0;
uint32_t lastCoulombSaveMs = 0;
float mahDischargedSinceFull = 0.0f;

void saveBatteryCapacity() {
  prefs.begin("batt", false);
  prefs.putInt("cap", batteryCapacityMah);
  prefs.end();
}

void saveCoulombState() {
  prefs.begin("batt", false);
  prefs.putFloat("soc_mah", batterySocMah);
  prefs.end();
}

// Викликається з sampleReadings() у .ino - тобто щоразу, коли оновлюється
// кеш показань, тим самим тактом.
void updateCoulombCounter() {
  int battIdx = findChannelByLabel("Battery");
  if (battIdx < 0) return;

  uint32_t nowMs = millis();
  if (lastCoulombMs == 0) { lastCoulombMs = nowMs; return; } // перший тік - лише стартова точка часу
  float dt_h = (nowMs - lastCoulombMs) / 3600000.0f;
  lastCoulombMs = nowMs;

  float current_mA = cachedReadings[battIdx].current_mA; // + зарядка, - розряд
  batterySocMah += current_mA * dt_h;
  if (current_mA < 0) mahDischargedSinceFull += -current_mA * dt_h;

  float ocv = estimateOpenCircuitVoltage(cachedReadings[battIdx].busVoltage_V, current_mA);
  bool changed = false;

  // Прив'язка "повна".
  if (current_mA >= 0 && ocv >= batterySoc100V) {
    batterySocMah = batteryCapacityMah;
    mahDischargedSinceFull = 0.0f;
    changed = true;
  }

  // Прив'язка "порожня" - і заразом уточнення ємності.
  if (current_mA <= 0 && ocv <= batterySoc0V) {
    // Довіряємо спостереженню лише якщо це був справді суттєвий (>30%
    // ємності) цикл розряду з моменту останньої прив'язки "повна" - інакше
    // короткий випадковий провал напруги міг би зіпсувати оцінку ємності.
    if (mahDischargedSinceFull > batteryCapacityMah * 0.3f) {
      float observedCapacity = mahDischargedSinceFull;
      batteryCapacityMah = (int)((1.0f - BATTERY_CAPACITY_LEARN_RATE) * batteryCapacityMah +
                                  BATTERY_CAPACITY_LEARN_RATE * observedCapacity);
      saveBatteryCapacity();
    }
    batterySocMah = 0.0f;
    mahDischargedSinceFull = 0.0f;
    changed = true;
  }

  if (batterySocMah < 0) batterySocMah = 0;
  if (batterySocMah > batteryCapacityMah) batterySocMah = batteryCapacityMah;

  if (changed || (nowMs - lastCoulombSaveMs >= COULOMB_SAVE_INTERVAL_MS)) {
    lastCoulombSaveMs = nowMs;
    saveCoulombState();
  }
}

// Викликається один раз із setup() у .ino, ПІСЛЯ першого sampleReadings()
// (потрібні реальні cachedReadings для стартової оцінки, якщо збереженого
// стану ще немає).
void loadCoulombState() {
  prefs.begin("batt", true);
  batterySocMah = prefs.getFloat("soc_mah", -1.0f);
  prefs.end();

  if (batterySocMah < 0.0f) {
    // Немає збереженого стану (перший запуск прошивки або після factory
    // reset) - стартуємо з вольтажної оцінки, це кращий старт, ніж сліпе
    // припущення "завжди повна".
    int battIdx = findChannelByLabel("Battery");
    if (battIdx >= 0) {
      float ocv = estimateOpenCircuitVoltage(cachedReadings[battIdx].busVoltage_V, cachedReadings[battIdx].current_mA);
      batterySocMah = batteryCapacityMah * (estimateBatteryChargePercentByVoltage(ocv) / 100.0f);
    } else {
      batterySocMah = batteryCapacityMah;
    }
  }
}

String buildUpsStatus() {
  int psuIdx  = findChannelByLabel("PSU in");
  int battIdx = findChannelByLabel("Battery");

  bool online = (psuIdx >= 0) && (cachedReadings[psuIdx].busVoltage_V > INPUT_PRESENT_V);
  bool battPresent = (battIdx >= 0) && (cachedReadings[battIdx].busVoltage_V > 0.5f);

  bool lowBatt = battPresent && (batterySocPercent() < LOW_BATTERY_SOC_PERCENT);

  String status = online ? "OL" : "OB";
  if (lowBatt) status += " LB";

  // CHRG/DISCHRG - за знаком струму на каналі "Battery" (вже пройшов
  // deadband у ina3221.h, тож 0 тут означає "струму практично немає";
  // знак уже фізично коректний - + зарядка, - розряд, перевірено тестом).
  if (battPresent) {
    float battCurrent_mA = cachedReadings[battIdx].current_mA;
    if (battCurrent_mA > 0) status += " CHRG";
    else if (battCurrent_mA < 0) status += " DISCHRG";
  }

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
    if (name == "battery.charge")  { out = String(batterySocPercent(), 0); return true; }
    if (name == "battery.runtime") {
      float battCurrent_mA = cachedReadings[battIdx].current_mA;
      if (battCurrent_mA < 0) { // реально розряджається - маємо чим оцінити
        // batterySocMah - це вже поточний залишок ємності в мАг напряму
        // (coulomb counter), множити на % більше не треба.
        float runtime_s = batterySocMah / fabsf(battCurrent_mA) * 3600.0f;
        out = String((uint32_t)runtime_s);
      } else {
        out = "0"; // на мережі або струм у деадбенді - надійної оцінки немає
      }
      return true;
    }
  }
  if (psuIdx >= 0) {
    if (name == "input.voltage") { out = String(cachedReadings[psuIdx].busVoltage_V, 2); return true; }
  }
  if (outIdx >= 0) {
    if (name == "output.voltage") { out = String(cachedReadings[outIdx].busVoltage_V, 2); return true; }
    // Струм і потужність на "PSU in"/"UPS out" вже фізично коректні (завжди
    // невід'ємні) завдяки ChannelCal::currentSign в ina3221.h - fabsf() тут
    // більше не потрібен.
    if (name == "output.current") { out = String(cachedReadings[outIdx].current_mA / 1000.0f, 3); return true; }
    if (name == "ups.power" || name == "ups.realpower") { out = String(cachedReadings[outIdx].power_mW / 1000.0f, 1); return true; }
    if (name == "ups.load") {
      float loadPct = cachedReadings[outIdx].power_mW / 1000.0f / UPS_RATED_POWER_W * 100.0f;
      out = String(loadPct, 0);
      return true;
    }
  }
  return false;
}

// Повний перелік змінних, які віддає LIST VAR (лише ті, для яких getNutVarValue поверне true)
const char* NUT_VAR_NAMES[] = {
  "device.type", "device.mfr", "ups.mfr", "ups.model", "ups.firmware", "ups.status",
  "battery.voltage", "battery.current", "battery.charge", "battery.runtime",
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
