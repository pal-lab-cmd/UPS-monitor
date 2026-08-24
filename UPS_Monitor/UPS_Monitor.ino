#include <WiFi.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include "config.h"
#include "ina3221.h"
#include "web_pages.h"

Preferences prefs;
AsyncWebServer server(80);
INA3221 ina;

bool apMode = false;

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

void setupRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", apMode ? WIFI_HTML : INDEX_HTML);
  });

  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", WIFI_HTML);
  });

  server.on("/wifi/save", HTTP_POST, [](AsyncWebServerRequest *request){
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

  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{\"channels\":[";
    for (int ch = 1; ch <= 3; ch++) {
      Reading r = ina.read(ch);
      json += "{\"label\":\"" + String(CH_CAL[ch-1].label) + "\",";
      json += "\"bus_V\":" + String(r.busVoltage_V, 3) + ",";
      json += "\"shunt_mV\":" + String(r.shuntVoltage_mV, 3) + ",";
      json += "\"current_mA\":" + String(r.current_mA, 1) + "}";
      if (ch < 3) json += ",";
    }
    json += "]}";
    request->send(200, "application/json", json);
  });
}

void setup() {
  Serial.begin(115200);
  delay(300);

  bool inaOk = ina.begin();
  Serial.println(inaOk ? "INA3221 OK" : "INA3221 НЕ ВІДПОВІДАЄ — перевір I2C підключення");

  if (!connectWiFi()) {
    Serial.println("WiFi не підключено, запускаю точку доступу для налаштування");
    startAPMode();
  }

  ElegantOTA.begin(&server, OTA_USER, OTA_PASS);
  setupRoutes();
  server.begin();
}

void loop() {
  ElegantOTA.loop();
}
