#pragma once
// визначаємо версію
#define FW_VERSION "0.0.8"
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
  {8.947f, 0.294f, "Battery"},
  {9.050f, 0.315f, "PSU in"},
  {8.966f, 0.322f, "UPS out"}
};

// Current below this magnitude (mA) is treated as 0 (noise floor / deadband)
#define CURRENT_DEADBAND_MA 50.0f

// ---------- WiFi ----------
#define WIFI_ATTEMPT_TIMEOUT_MS 10000       // how long to wait for a single connect attempt
#define WIFI_RETRY_INTERVAL_MS  15000       // pause between attempts
#define WIFI_RETRY_TOTAL_MS     (5UL * 60 * 1000) // give up and go to AP after this long
#define WIFI_CHECK_INTERVAL_MS  10000       // how often loop() checks the link is still up
#define AP_SSID "UPS-Monitor-Setup"
#define AP_PASS "12345678"   // CHANGE before deploying — min 8 chars for WPA2

// ---------- Auth (protects WiFi save, reboot, OTA, password change) ----------
// /api/data (telemetry) stays open — no reason to gate battery/current readings.
// Username is always "admin"; only the password is configurable, stored in
// Preferences ("auth"/"pass"). On first boot (no stored value) this default is used.
#define AUTH_USER "admin"
#define AUTH_DEFAULT_PASS "change-me"   // CHANGE before deploying

// Hold the BOOT button (GPIO0) low for this long during normal operation to
// factory-reset WiFi credentials AND the settings password back to defaults.
// Requires physical access to the board — intentional, see firmware notes.
#define FACTORY_RESET_PIN 0
#define FACTORY_RESET_HOLD_MS 5000
