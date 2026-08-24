#pragma once
// визначаємо версію
#define FW_VERSION "0.0.3"
// ---------- I2C / INA3221 ----------
#define I2C_SDA 8
#define I2C_SCL 9
#define INA3221_ADDR 0x40

// Per-channel calibration (from your 2-point calibration)
struct ChannelCal {
  float rShunt_mOhm;
  float offset_mV;
  const char* label;
};

static const ChannelCal CH_CAL[3] = {
  {8.947f, 0.294f, "PSU in"},
  {9.050f, 0.315f, "NAS out"},
  {8.966f, 0.322f, "Battery"}
};

// Current below this magnitude (mA) is treated as 0 (noise floor / deadband)
#define CURRENT_DEADBAND_MA 50.0f

// ---------- WiFi ----------
#define WIFI_ATTEMPT_TIMEOUT_MS 10000       // how long to wait for a single connect attempt
#define WIFI_RETRY_INTERVAL_MS  15000       // pause between attempts
#define WIFI_RETRY_TOTAL_MS     (5UL * 60 * 1000) // give up and go to AP after this long
#define AP_SSID "UPS-Monitor-Setup"
#define AP_PASS "12345678"   // CHANGE before deploying — min 8 chars for WPA2

// ---------- OTA ----------
#define OTA_USER "admin"
#define OTA_PASS "change-me"   // CHANGE before deploying
