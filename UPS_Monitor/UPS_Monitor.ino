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

Preferences prefs;
AsyncWebServer server(80);
INA3221 ina;

bool apMode = false;
String authPass;                 // поточний пароль доступу (RAM-кеш значення з Preferences)
uint32_t lastWifiCheckMs = 0;

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

  // Форма теж захищена паролем - на ній немає даних для показу, лише зміна налаштувань.
  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;
    request->send_P(200, "text/html", WIFI_HTML);
  });

  server.on("/wifi/save", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;

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

  // Зміна пароля доступу до налаштувань (той самий пароль, що й для OTA).
  server.on("/api/set-password", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;

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

  // Ендпоінт для ручного перезавантаження з UI
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!requireAuth(request)) return;

    request->send(200, "text/plain", "Перезавантаження...");
    delay(1500); // Даємо час на відправку пакета перед рестартом
    ESP.restart();
  });

  // Телеметрія - навмисно БЕЗ пароля, це лише читання стану батареї/струму/напруги.
  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["version"] = FW_VERSION;
    JsonArray channels = doc["channels"].to<JsonArray>();

    for (int ch = 1; ch <= 3; ch++) {
      Reading r = ina.read(ch);
      JsonObject c = channels.add<JsonObject>();
      c["label"] = CH_CAL[ch - 1].label;
      c["bus_V"] = serialized(String(r.busVoltage_V, 3));
      c["shunt_mV"] = serialized(String(r.shuntVoltage_mV, 3));
      c["current_mA"] = serialized(String(r.current_mA, 1));
      c["power_mW"] = serialized(String(r.power_mW, 1));
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

  bool inaOk = ina.begin();
  Serial.println(inaOk ? "INA3221 OK" : "INA3221 НЕ ВІДПОВІДАЄ — перевір I2C підключення");

  if (!connectWiFi()) {
    Serial.println("WiFi не підключено, запускаю точку доступу для налаштування");
    startAPMode();
  }

  ElegantOTA.begin(&server, AUTH_USER, authPass.c_str());

  setupRoutes();
  server.begin();
}

void loop() {
  ElegantOTA.loop();
  checkWiFiConnection();
}
