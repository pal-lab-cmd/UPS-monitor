#pragma once
// визначаємо версію
#define FW_VERSION "0.1.3"
// ---------- I2C / INA3221 ----------
#define I2C_SDA 8
#define I2C_SCL 9
#define INA3221_ADDR 0x40

// Per-channel calibration (from your 2-point calibration)
struct ChannelCal {
  float rShunt_mOhm;
  float offset_mV;
  const char* label;
  // +1 або -1 - виправляє полярність шунта так, щоб знак струму відповідав
  // фізичному змісту: для "Battery" позитивний = зарядка (підтверджено
  // тестом - відключення PSU дало від'ємні значення при розряді, це вже
  // правильно, тож тут +1). Для "PSU in" і "UPS out" струм фізично завжди
  // однонаправлений (обидва можуть тільки віддавати струм, не приймати) -
  // за реальними вимірами шунт дає від'ємні значення в нормальному режимі,
  // тож тут -1, щоб на виході завжди був фізично коректний позитивний струм.
  float currentSign;
};

static const ChannelCal CH_CAL[3] = {
  {8.947f, 0.294f, "Battery",  1.0f},
  {9.050f, 0.315f, "PSU in",  -1.0f},
  {8.966f, 0.322f, "UPS out", -1.0f}
};

// Current below this magnitude (mA) is treated as 0 (noise floor / deadband)
#define CURRENT_DEADBAND_MA 50.0f

// Як часто оновлювати кеш показань INA3221 (мс) - і HTTP, і NUT читають
// лише з цього кешу, щоб не смикати I2C одночасно з різних контекстів.
#define SAMPLE_INTERVAL_MS 1000

// ---------- NUT (Network UPS Tools) сервер ----------
#define NUT_PORT 3493
#define NUT_UPS_NAME "esp32ups"   // саме цю назву виберете в NUT-клієнтах при налаштуванні
#define NUT_UPS_DESC "DIY ESP32-S3 UPS Monitor"
#define NUT_USER "monuser"
#define NUT_PASS "change-me"      // CHANGE before deploying

// ---------- Другий ("дзеркальний") NUT-пристрій — сумісність з QNAP QTS ----------
// QNAP QTS у режимі "Network UPS slave" підключається під жорстко вшитим
// (не редагованим у GUI) ім'ям "qnapups". За фактом (перевірено через лог
// обміну в /api/nutlog) реальний QNAP-клієнт - це "upsutil", а не
// стандартний upsmon: він взагалі не шле USERNAME/PASSWORD/LOGIN, одразу
// після підключення робить LIST VAR / GET VAR. Тобто окремих креденшлів
// для нього не існує в природі - раніше тут були NUT_USER_QNAP/
// NUT_PASS_QNAP "про запас", але оскільки жоден реальний клієнт їх ніколи
// не надішле (а requireAuth для qnapups вимкнено, див. nut_server.h), вони
// прибрані як мертвий код. "Read-only" для qnapups тримається не на
// паролі, а структурно - сервер нижче не реалізує SET/INSTCMD взагалі.
#define NUT_UPS_NAME_QNAP "qnapups"
#define NUT_UPS_DESC_QNAP "DIY ESP32-S3 UPS Monitor (QNAP compat)"

// ---------- Характеристики UPS (для ups.status / ups.load) ----------
#define UPS_RATED_POWER_W  100.0f
// Поріг "low battery" (LB) для ups.status - у % заряду, а не у вольтах,
// щоб він автоматично залишався коректним при перекалібруванні напруг
// 100%/0% через /settings/battery (див. batterySocPercent() у nut_server.h).
#define LOW_BATTERY_SOC_PERCENT 20.0f
#define INPUT_PRESENT_V    8.0f    // вище цього значення вважаємо, що PSU/мережа є

// Внутрішній опір батарейного пакета (Ом) - для компенсації просідання
// напруги під навантаженням при оцінці SOC. Без цього % різко "падає"
// в момент підключення навантаження (через IR-просідання напруги), а не
// через реально витрачену ємність - саме це ви й спостерігали. Формула:
// V_спокою = V_виміряна - (current_mA/1000) * R, де current_mA зі знаком
// (+ зарядка, - розряд) - працює в обидва боки одним рівнянням.
// Стартове наближення для 4S2P LiFePO4 (8 елементів по 4000мАг/12.8Вт-год);
// уточніть емпірично: прикладіть відоме навантаження і зміряйте ΔV/ΔI.
#define BATTERY_INTERNAL_RESISTANCE_OHM 0.05f

// ---------- Coulomb counting (облік заряду інтегруванням струму) ----------
// Як часто зберігати накопичений стан заряду (мАг) у Preferences, щоб не
// зношувати флеш щосекунди. Стан також зберігається одразу в моменти
// калібрувальних "прив'язок" (досягнення SOC100/SOC0 за напругою).
#define COULOMB_SAVE_INTERVAL_MS (5UL * 60 * 1000) // 5 хвилин

// Наскільки сильно кожна нова "прив'язка SOC0" підлаштовує збережену
// ємність (batteryCapacityMah) під реально спостережену ємність останнього
// циклу розряду (від прив'язки SOC100 до прив'язки SOC0). 0.2 = нове
// спостереження отримує 20% ваги, старе значення - 80% (експоненційне
// згладжування, стійке до одного нетипового циклу розряду).
#define BATTERY_CAPACITY_LEARN_RATE 0.2f

// ---------- Батарея (значення за замовчуванням - реальні редагуються через
// /settings/battery і зберігаються в Preferences, namespace "batt") ----------
// LiFePO4 4S2P: 4000мАг на елемент x 2 паралельно = 8000мАг сумарно.
#define BATTERY_CAPACITY_MAH_DEFAULT 8000
#define BATTERY_SOC100_V_DEFAULT     13.94f  // напруга при 100% заряду
#define BATTERY_SOC0_V_DEFAULT       10.60f  // напруга при 0% заряду (поріг BMS)

// ---------- NTP / час (сервер і часовий пояс редагуються через
// /settings/ntp, зберігаються в Preferences, namespace "time") ----------
#define NTP_SERVER1_DEFAULT "pool.ntp.org"
#define NTP_SERVER2 "time.google.com"        // резервний сервер, не редагується через UI
// POSIX TZ рядок за замовчуванням - Europe/Kyiv (EET/EEST з автоматичним DST)
#define DEFAULT_TZ_POSIX "EET-2EEST,M3.5.0/3,M10.5.0/4"

// ---------- Зберігання історії (саму історію ще не реалізовано - це лише
// налаштування терміну зберігання наперед, для майбутньої функції) ----------
#define HISTORY_RETENTION_DAYS_DEFAULT 90  // 3 місяці

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
//
// All /settings/* mutating routes (and /wifi/save, /api/reboot) skip the auth
// check entirely while the device is in AP mode (apMode == true) - i.e. on
// first boot before WiFi is configured, or right after a factory reset. This
// is intentional: at that point you already need physical/WiFi proximity to
// the device to even reach the setup page, so an extra password step just
// adds friction without adding real security. Once connected to real WiFi,
// every one of these routes requires the password again as normal.
#define AUTH_USER "admin"
#define AUTH_DEFAULT_PASS "change-me"   // CHANGE before deploying

// Hold the BOOT button (GPIO0) low for this long during normal operation to
// factory-reset WiFi credentials AND the settings password back to defaults.
// Requires physical access to the board — intentional, see firmware notes.
#define FACTORY_RESET_PIN 0
#define FACTORY_RESET_HOLD_MS 5000
