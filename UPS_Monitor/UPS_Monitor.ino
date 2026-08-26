#include <WiFi.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
// Вказуємо ElegantOTA використовувати асинхронний сервер,
// щоб уникнути конфлікту HTTP-методів
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#include <ElegantOTA.h>
#include <ArduinoJson.h>   // Tools -> Manage Libraries -> "ArduinoJson" (Benoit Blanchon)
#include "config.h"
#include "ina3221.h"
#include "web_pages.h"
#include "nut_server.h"

Preferences prefs;
AsyncWebServer server(80);
INA3221 ina;

bool apMode = false;
String authPass;                 // поточний пароль доступу (RAM-кеш значення з Preferences)
uint32_t lastWifiCheckMs = 0;

// Runtime-налаштування батареї (config.h містить лише дефолти, реальні
// значення - тут, завантажуються з Preferences namespace "batt")
int   batteryCapacityMah;
float batterySoc100V;
float batterySoc0V;
float batterySocMah;   // поточний стан заряду (coulomb counter, мАг) - див. nut_server.h

void loadBatterySettings() {
  prefs.begin("batt", true);
  batteryCapacityMah = prefs.getInt("cap", BATTERY_CAPACITY_MAH_DEFAULT);
  batterySoc100V      = prefs.getFloat("v100", BATTERY_SOC100_V_DEFAULT);
  batterySoc0V         = prefs.getFloat("v0", BATTERY_SOC0_V_DEFAULT);
  prefs.end();
}

// Runtime-налаштування NTP/часу (namespace "time")
String loadNtpServer() {
  prefs.begin("time", true);
  String s = prefs.getString("srv", NTP_SERVER1_DEFAULT);
  prefs.end();
  return s;
}
String loadTz() {
  prefs.begin("time", true);
  String s = prefs.getString("tz", DEFAULT_TZ_POSIX);
  prefs.end();
  return s;
}

void setupTime() {
  String ntpServer = loadNtpServer();
  String tz = loadTz();
  configTzTime(tz.c_str(), ntpServer.c_str(), NTP_SERVER2);
  Serial.printf("NTP: сервер=%s, TZ=%s\n", ntpServer.c_str(), tz.c_str());
}

// Термін зберігання історії (namespace "hist") - сама історія ще не
// реалізована, це лише налаштування наперед для майбутньої функції.
int loadHistoryRetentionDays() {
  prefs.begin("hist", true);
  int days = prefs.getInt("days", HISTORY_RETENTION_DAYS_DEFAULT);
  prefs.end();
  return days;
}

// Кеш показань усіх трьох каналів - і HTTP /api/data, і NUT-сервер читають
// лише звідси, а не смикають I2C напряму з кожного запиту (обидва можуть
// виконуватись у різних async-контекстах, паралельний I2C - ризик).
Reading cachedReadings[3];
uint32_t lastSampleMs = 0;

void forceSample() {
  lastSampleMs = millis();
  for (int ch = 1; ch <= 3; ch++) {
    cachedReadings[ch - 1] = ina.read(ch);
  }
  updateCoulombCounter();
}

void sampleReadings() {
  if (millis() - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  forceSample();
}

// ---------- Factory reset (фізична кнопка BOOT, потребує доступу до плати) ----------
void checkFactoryReset() {
  pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
  if (digitalRead(FACTORY_RESET_PIN) != LOW) return; // кнопка не затиснута при старті

  Serial.println("BOOT затиснуто при старті - тримайте для скидання налаштувань...");
  uint32_t pressStart = millis();
  while (digitalRead(FACTORY_RESET_PIN) == LOW) {
    if (millis() - pressStart >= FACTORY_RESET_HOLD_MS) {
      Serial.println("Скидаю WiFi та пароль доступу до заводських значень...");
      prefs.begin("wifi", false); prefs.clear(); prefs.end();
      prefs.begin("auth", false); prefs.clear(); prefs.end();
      Serial.println("Готово. Перезавантаження...");
      delay(300);
      ESP.restart();
    }
    delay(50);
  }
  Serial.println("Кнопку відпущено зарано - скидання скасовано.");
}

// ---------- Auth ----------
void loadAuthPass() {
  prefs.begin("auth", true);
  authPass = prefs.getString("pass", AUTH_DEFAULT_PASS);
  prefs.end();
}

void saveAuthPass(const String &newPass) {
  prefs.begin("auth", false);
  prefs.putString("pass", newPass);
  prefs.end();
  authPass = newPass;
}

// Повертає true, якщо запит авторизований. Якщо ні - сама надсилає 401 і
// повертає false, виклик у роуті має одразу зробити return.
//
// ВАЖЛИВО: без явного AUTH_BASIC тут бібліотека за замовчуванням шле
// Digest-виклик, на який деякі браузери (Firefox) не показують штатне вікно
// логіну і замість цього виводять "Looks like there's a problem with this
// site" - тому метод вказуємо явно.
bool requireAuth(AsyncWebServerRequest *request) {
  if (request->authenticate(AUTH_USER, authPass.c_str())) return true;
  request->requestAuthentication(AsyncAuthType::AUTH_BASIC, "UPS Monitor");
  return false;
}

// Для всіх /settings/* та /wifi/save: пароль не питаємо, поки пристрій у
// AP-режимі (перше налаштування або щойно після factory reset) - на цьому
// етапі доступ до пристрою вже й так вимагає фізичної/WiFi близькості, тож
// зайвий пароль лише додає тертя. Щойно підключились до реальної мережі -
// пароль знову обов'язковий на кожному з цих маршрутів.
bool requireAuthUnlessSetup(AsyncWebServerRequest *request) {
  if (apMode) return true;
  return requireAuth(request);
}

// ---------- WiFi ----------
void startAPMode() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP режим для налаштування. IP: ");
  Serial.println(WiFi.softAPIP());
}

bool connectWiFi() {
  prefs.begin("wifi", true); // read-only
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.isEmpty()) return false;

  WiFi.mode(WIFI_STA);
  uint32_t retryStart = millis();

  while (millis() - retryStart < WIFI_RETRY_TOTAL_MS) {
    Serial.printf("WiFi: спроба підключення до %s...\n", ssid.c_str());
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t attemptStart = millis();
    wl_status_t status;
    while ((status = WiFi.status()) != WL_CONNECTED &&
           millis() - attemptStart < WIFI_ATTEMPT_TIMEOUT_MS) {
      delay(250);
    }

    if (status == WL_CONNECTED) {
      Serial.print("Підключено. IP: ");
      Serial.println(WiFi.localIP());
      return true;
    }

    if (status == WL_CONNECT_FAILED) {
      // Wrong password — retrying the same creds won't help, bail out now
      Serial.println("Невірний пароль WiFi — переходжу в режим налаштування");
      return false;
    }

    Serial.printf("Невдача (status=%d), повтор через %lus...\n", status, WIFI_RETRY_INTERVAL_MS / 1000);
    delay(WIFI_RETRY_INTERVAL_MS);
  }

  Serial.println("WiFi недоступний 5 хвилин — переходжу в режим налаштування");
  return false;
}

// Ненав'язлива перевірка з loop(): якщо ми в STA-режимі і з'єднання впало,
// пробуємо перепідключитись без блокування (WiFi.reconnect() використовує
// востаннє задані credentials і повертає керування одразу).
void checkWiFiConnection() {
  if (apMode) return;
  if (millis() - lastWifiCheckMs < WIFI_CHECK_INTERVAL_MS) return;
  lastWifiCheckMs = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi: з'єднання втрачено, пробую перепідключитись...");
    WiFi.reconnect();
  }
}

void setupRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", apMode ? WIFI_HTML : INDEX_HTML);
  });

  // ---------- WiFi ----------
  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;
    request->send_P(200, "text/html", WIFI_HTML);
  });

  server.on("/wifi/save", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;

    String ssid, pass;
    if (request->hasParam("ssid", true)) ssid = request->getParam("ssid", true)->value();
    if (request->hasParam("pass", true)) pass = request->getParam("pass", true)->value();

    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    request->send(200, "text/plain", "Збережено. Перезавантаження...");
    delay(500);
    ESP.restart();
  });

  // ---------- Пароль доступу ----------
  server.on("/settings/password", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;
    request->send_P(200, "text/html", PASSWORD_HTML);
  });

  server.on("/api/set-password", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;

    if (!request->hasParam("newpass", true)) {
      request->send(400, "text/plain", "Відсутній параметр newpass");
      return;
    }
    String newPass = request->getParam("newpass", true)->value();
    if (newPass.length() < 8) {
      request->send(400, "text/plain", "Пароль має бути не коротшим за 8 символів");
      return;
    }

    saveAuthPass(newPass);
    // ElegantOTA перевіряє пароль, з яким був ініціалізований при begin() -
    // перезавантажуємось, щоб він підхопив новий пароль так само, як WiFi.
    request->send(200, "text/plain", "Пароль збережено. Перезавантаження...");
    delay(500);
    ESP.restart();
  });

  // ---------- NTP / час ----------
  server.on("/settings/ntp", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;
    request->send_P(200, "text/html", NTP_HTML);
  });

  server.on("/settings/ntp/save", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;

    String srv = NTP_SERVER1_DEFAULT;
    String tz  = DEFAULT_TZ_POSIX;
    if (request->hasParam("srv", true)) srv = request->getParam("srv", true)->value();
    if (request->hasParam("tz", true))  tz  = request->getParam("tz", true)->value();

    prefs.begin("time", false);
    prefs.putString("srv", srv);
    prefs.putString("tz", tz);
    prefs.end();

    setupTime(); // застосовуємо одразу, без перезавантаження
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/ntp-settings", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;
    JsonDocument doc;
    doc["srv"] = loadNtpServer();
    doc["tz"] = loadTz();
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // ---------- Батарея ----------
  server.on("/settings/battery", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;
    request->send_P(200, "text/html", BATTERY_HTML);
  });

  server.on("/settings/battery/save", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;

    int oldCap = batteryCapacityMah;
    if (request->hasParam("cap", true)) batteryCapacityMah = request->getParam("cap", true)->value().toInt();
    if (request->hasParam("v100", true)) batterySoc100V = request->getParam("v100", true)->value().toFloat();
    if (request->hasParam("v0", true))   batterySoc0V   = request->getParam("v0", true)->value().toFloat();

    // Ємність змінилась вручну - перемасштабовуємо накопичений стан заряду
    // так, щоб зберегти поточний %, а не трактувати ті самі мАг як інший %.
    if (oldCap > 0 && batteryCapacityMah != oldCap) {
      batterySocMah = batterySocMah / oldCap * batteryCapacityMah;
    }

    prefs.begin("batt", false);
    prefs.putInt("cap", batteryCapacityMah);
    prefs.putFloat("v100", batterySoc100V);
    prefs.putFloat("v0", batterySoc0V);
    prefs.putFloat("soc_mah", batterySocMah);
    prefs.end();

    request->send(200, "text/plain", "OK");
  });

  server.on("/api/battery-settings", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;
    JsonDocument doc;
    doc["cap"] = batteryCapacityMah;
    doc["v100"] = serialized(String(batterySoc100V, 2));
    doc["v0"] = serialized(String(batterySoc0V, 2));
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // ---------- Зберігання історії (лише налаштування, сама історія - пізніше) ----------
  server.on("/settings/history", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;
    request->send_P(200, "text/html", HISTORY_HTML);
  });

  server.on("/settings/history/save", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;

    int days = HISTORY_RETENTION_DAYS_DEFAULT;
    if (request->hasParam("days", true)) days = request->getParam("days", true)->value().toInt();

    prefs.begin("hist", false);
    prefs.putInt("days", days);
    prefs.end();

    request->send(200, "text/plain", "OK");
  });

  server.on("/api/history-settings", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;
    JsonDocument doc;
    doc["days"] = loadHistoryRetentionDays();
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // Ендпоінт для ручного перезавантаження з UI
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;

    request->send(200, "text/plain", "Перезавантаження...");
    delay(1500); // Даємо час на відправку пакета перед рестартом
    ESP.restart();
  });

  // ---------- Settings hub ----------
  // ВАЖЛИВО: реєструємо цей маршрут ПІСЛЯ всіх конкретніших /settings/*
  // маршрутів вище. У ESPAsyncWebServer canHandle() матчить не лише точний
  // URI, а й будь-який шлях, що починається з "<uri>/" - тобто хендлер на
  // "/settings" сам собою матчить і "/settings/battery", і "/settings/ntp"
  // тощо. Хендлери перевіряються в порядку реєстрації, перший збіг виграє,
  // тож якщо цей загальний маршрут зареєструвати першим (як було раніше),
  // він перехоплює запити до всіх підсторінок раніше, ніж вони доходять до
  // своїх власних, конкретніших хендлерів.
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuthUnlessSetup(request)) return;
    request->send_P(200, "text/html", SETTINGS_HTML);
  });

  // Телеметрія - навмисно БЕЗ пароля, це лише читання стану батареї/струму/напруги.
  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["version"] = FW_VERSION;

    time_t now = time(nullptr);
    doc["time"] = (uint32_t)now;
    doc["time_synced"] = (now > 100000); // грубий, але надійний маркер "NTP ще не відповів"

    JsonArray channels = doc["channels"].to<JsonArray>();

    for (int ch = 1; ch <= 3; ch++) {
      Reading r = cachedReadings[ch - 1];
      JsonObject c = channels.add<JsonObject>();
      c["label"] = CH_CAL[ch - 1].label;
      c["bus_V"] = serialized(String(r.busVoltage_V, 3));
      c["shunt_mV"] = serialized(String(r.shuntVoltage_mV, 3));
      c["current_mA"] = serialized(String(r.current_mA, 1));
      c["power_mW"] = serialized(String(r.power_mW, 1));

      if (strcmp(CH_CAL[ch - 1].label, "Battery") == 0) {
        c["soc_percent"] = serialized(String(batterySocPercent(), 0));
      }
      if (strcmp(CH_CAL[ch - 1].label, "UPS out") == 0) {
        // r.power_mW тут вже фізично коректний (невід'ємний) завдяки
        // ChannelCal::currentSign в ina3221.h - fabsf() більше не потрібен.
        float loadPct = r.power_mW / 1000.0f / UPS_RATED_POWER_W * 100.0f;
        c["load_percent"] = serialized(String(loadPct, 0));
      }
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
}

void setup() {
  Serial.begin(115200);
  delay(300);

  checkFactoryReset();
  loadAuthPass();
  loadBatterySettings();

  bool inaOk = ina.begin();
  Serial.println(inaOk ? "INA3221 OK" : "INA3221 НЕ ВІДПОВІДАЄ — перевір I2C підключення");

  forceSample();         // перше заповнення кешу - потрібне вже тут для стартової оцінки SOC нижче
  loadCoulombState();

  if (connectWiFi()) {
    setupTime(); // NTP має сенс лише за наявності інтернету, тож саме тут
  } else {
    Serial.println("WiFi не підключено, запускаю точку доступу для налаштування");
    startAPMode();
  }

  ElegantOTA.begin(&server, AUTH_USER, authPass.c_str());

  setupRoutes();
  server.begin();

  nutServerBegin();
}

void loop() {
  ElegantOTA.loop();
  checkWiFiConnection();
  sampleReadings();
}
