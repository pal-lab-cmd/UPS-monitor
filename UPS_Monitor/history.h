#pragma once
#include <FFat.h>
#include <time.h>
#include <ArduinoJson.h>
#include "config.h"
#include "ina3221.h" // для типу Reading

// ============================================================================
// Історія показань для графіків - дворівневий "round-robin" буфер поверх
// FATFS-розділу (партиція обрана в Arduino IDE: 16M Flash, 3MB APP /
// 9.9MB FATFS -> монтується через бібліотеку FFat).
//
// Обидва рівні - файли ФІКСОВАНОГО розміру, які самі перезаписують
// найстаріші записи по колу (як rrdtool/Cacti), тому місце на флеші НЕ
// росте необмежено, навіть якщо пристрій пропрацює роками без перезаливки.
//
//   detail : 1 запис/хв,  вікно HISTORY_DETAIL_DAYS днів - для "наближеного"
//            перегляду останніх кількох днів.
//   long   : 1 запис/HISTORY_LONG_INTERVAL_S сек, вікно до
//            HISTORY_RETENTION_DAYS_MAX днів (жорсткий кап розміру файлу) -
//            для графіків "за весь термін зберігання".
//
// Реальний обраний користувачем термін зберігання (Preferences "hist"/
// "days", за замовчуванням HISTORY_RETENTION_DAYS_DEFAULT з config.h) НЕ
// змінює розмір файлу "long" - він лише відсікає видачу при читанні
// (записи старіші за нього ігноруються). Завдяки цьому зміна терміну в
// налаштуваннях не вимагає перестворення файлу й не губить історію.
//
// Потужність НЕ зберігається окремим полем - вона легко рахується на
// льоту (V*I) для будь-якого з трьох каналів під час малювання графіка;
// зберігаються лише напруга та струм (див. HistSample нижче).
// ============================================================================

#define HISTORY_DETAIL_DAYS        4            // роздільність 1 хв
#define HISTORY_DETAIL_INTERVAL_S  60UL
#define HISTORY_LONG_INTERVAL_S    (15UL * 60)  // роздільність 15 хв
#define HISTORY_RETENTION_DAYS_MAX 365          // жорсткий кап розміру файлу "long"

#define HISTORY_DETAIL_CAPACITY  ((uint32_t)((HISTORY_DETAIL_DAYS * 86400UL) / HISTORY_DETAIL_INTERVAL_S))
#define HISTORY_LONG_CAPACITY    ((uint32_t)((HISTORY_RETENTION_DAYS_MAX * 86400UL) / HISTORY_LONG_INTERVAL_S))

#define HISTORY_DETAIL_PATH "/hist_detail.bin"
#define HISTORY_LONG_PATH   "/hist_long.bin"

// 17 байт, без вирівнювання. ts==0 означає "порожній слот" (ще не
// записаний) - використовується при першому заповненні кільця по колу.
struct __attribute__((packed)) HistSample {
  uint32_t ts;        // unix-час запису (UTC), сек
  uint16_t battV_cV;  // напруга батареї * 100 (сантивольти)
  int16_t  battI_mA;
  uint16_t psuV_cV;
  int16_t  psuI_mA;
  uint16_t upsV_cV;
  int16_t  upsI_mA;
  uint8_t  socPct;    // 0..100
};
static_assert(sizeof(HistSample) == 17, "HistSample: розмір змінився - формат файлу історії теж треба міняти (стара історія стане нечитабельною)");

// Чи синхронізований системний час (NTP). Без цього писати мітки часу
// в історію немає сенсу - вони будуть сміттям (епоха ~1970).
inline bool historyTimeSynced() {
  return time(nullptr) > 1700000000UL; // будь-яка дата з 2023+ як орієнтир "час реальний"
}

// ---------- Кільцевий буфер фіксованого розміру у файлі на FFat ----------
class HistRing {
public:
  bool begin(const char* path, uint32_t capacity) {
    _path = path;
    _capacity = capacity;
    size_t expectedSize = (size_t)capacity * sizeof(HistSample);

    bool exists = FFat.exists(path);
    bool sizeOk = false;
    if (exists) {
      File f = FFat.open(path, "r");
      if (f) { sizeOk = (f.size() == expectedSize); f.close(); }
    }
    if (!exists || !sizeOk) {
      // Перший запуск, або розмір не збігається (напр. змінили константи
      // капацity у коді) - перестворюємо файл нулями. Стара історія ЦЬОГО
      // рівня при цьому губиться (інший рівень не зачіпається).
      if (!createEmptyFile(expectedSize)) return false;
    }

    _file = FFat.open(path, "r+"); // "r+" - НЕ обрізає файл (на відміну від FILE_WRITE="w+")
    if (!_file) return false;
    _head = findHead();
    return true;
  }

  void append(const HistSample& s) {
    if (!_file) return;
    _file.seek((uint64_t)_head * sizeof(HistSample));
    _file.write((const uint8_t*)&s, sizeof(HistSample));
    _file.flush();
    _head = (_head + 1) % _capacity;
  }

  uint32_t capacity() const { return _capacity; }
  size_t fileSizeBytes() const { return (size_t)_capacity * sizeof(HistSample); }

  // Читає весь файл РІВНО ОДИН РАЗ, стартуючи з позиції "голови" (_head).
  // Це дає хронологічний порядок безкоштовно, без сортування: якщо кільце
  // ще не заповнилось по колу, слоти [_head, capacity) порожні (ts==0,
  // відфільтруються самі), а [0, _head) - вже хронологічні дані; якщо
  // заповнилось - [_head, capacity) це найстаріші дані, [0, _head) -
  // найновіші, разом рівно один хронологічний прохід.
  //
  // stride - проріджування: зберігається лише кожен stride-й запис, що
  // потрапив у діапазон, тож у RAM ніколи не тримаємо більше maxOut
  // записів одночасно (для рівня "long" це 35040 записів у файлі, але
  // читаємо блоками по 64, а не все одразу).
  size_t queryChronological(uint32_t fromTs, uint32_t toTs, uint32_t stride, HistSample* out, size_t maxOut) {
    if (!_file || maxOut == 0) return 0;
    if (stride == 0) stride = 1;

    size_t n = 0;
    uint32_t matched = 0;
    static HistSample buf[64];
    uint32_t pos = _head;
    uint32_t remaining = _capacity;

    while (remaining > 0 && n < maxOut) {
      uint32_t want = min((uint32_t)64, min(remaining, _capacity - pos));
      _file.seek((uint64_t)pos * sizeof(HistSample));
      size_t got = _file.read((uint8_t*)buf, want * sizeof(HistSample)) / sizeof(HistSample);
      if (got == 0) break;

      for (uint32_t i = 0; i < got; i++) {
        if (buf[i].ts != 0 && buf[i].ts >= fromTs && buf[i].ts <= toTs) {
          if (matched % stride == 0 && n < maxOut) out[n++] = buf[i];
          matched++;
        }
      }
      pos = (pos + got) % _capacity;
      remaining -= got;
    }
    return n;
  }

private:
  const char* _path = nullptr;
  uint32_t _capacity = 0;
  uint32_t _head = 0;
  File _file;

  bool createEmptyFile(size_t sizeBytes) {
    File f = FFat.open(_path, "w+"); // тут навмисно обрізаємо/створюємо
    if (!f) return false;
    static uint8_t zeroBuf[512] = {0};
    size_t remaining = sizeBytes;
    while (remaining > 0) {
      size_t chunk = remaining < sizeof(zeroBuf) ? remaining : sizeof(zeroBuf);
      if (f.write(zeroBuf, chunk) != chunk) { f.close(); return false; }
      remaining -= chunk;
    }
    f.close();
    return true;
  }

  // Одноразове (при begin()) сканування файлу для визначення позиції
  // "голови" (куди писати наступний запис): перший порожній слот (ts==0),
  // якщо кільце ще не заповнилось по колу, інакше - слот одразу за
  // записом з найновішим ts. Читає блоками по 64 записи (не по одному),
  // щоб не було ~35000 окремих файлових операцій при HISTORY_LONG_CAPACITY.
  uint32_t findHead() {
    _file.seek(0);
    static HistSample buf[64];
    uint32_t bestIdx = 0, bestTs = 0;
    int64_t firstEmpty = -1;
    uint32_t idx = 0;
    while (idx < _capacity) {
      uint32_t want = min((uint32_t)64, _capacity - idx);
      size_t got = _file.read((uint8_t*)buf, want * sizeof(HistSample)) / sizeof(HistSample);
      if (got == 0) break;
      for (uint32_t i = 0; i < got; i++) {
        uint32_t ts = buf[i].ts;
        if (ts == 0) {
          if (firstEmpty < 0) firstEmpty = idx + i;
        } else if (ts >= bestTs) {
          bestTs = ts;
          bestIdx = idx + i;
        }
      }
      idx += got;
    }
    if (firstEmpty >= 0) return (uint32_t)firstEmpty;
    return (bestIdx + 1) % _capacity;
  }
};

// ---------- Акумулятори: усереднення "сирих" показань (SAMPLE_INTERVAL_MS)
// у записи detail/long рівнів ----------
struct HistAccum {
  uint32_t n = 0;
  double battV = 0, battI = 0, psuV = 0, psuI = 0, upsV = 0, upsI = 0, soc = 0;

  void add(const Reading& batt, const Reading& psu, const Reading& ups, float socPercent) {
    battV += batt.busVoltage_V; battI += batt.current_mA;
    psuV  += psu.busVoltage_V;  psuI  += psu.current_mA;
    upsV  += ups.busVoltage_V;  upsI  += ups.current_mA;
    soc   += socPercent;
    n++;
  }

  bool toSample(HistSample& out, uint32_t ts) const {
    if (n == 0) return false;
    out.ts       = ts;
    out.battV_cV = (uint16_t)lround((battV / n) * 100.0);
    out.battI_mA = (int16_t)lround(battI / n);
    out.psuV_cV  = (uint16_t)lround((psuV / n) * 100.0);
    out.psuI_mA  = (int16_t)lround(psuI / n);
    out.upsV_cV  = (uint16_t)lround((upsV / n) * 100.0);
    out.upsI_mA  = (int16_t)lround(upsI / n);
    out.socPct   = (uint8_t)lround(soc / n);
    return true;
  }

  void reset() { *this = HistAccum(); }
};

HistRing histDetail;
HistRing histLong;
static HistAccum g_detailAccum;
static HistAccum g_longAccum;
static uint32_t g_lastDetailFlush = 0;
static uint32_t g_lastLongFlush = 0;

// Термін зберігання, обраний користувачем (днів). Встановлюється ЗЗОВНІ
// через historyBegin(retentionDays) при старті і historySetRetentionDays()
// при зміні в /settings/history - сам history.h НЕ читає/пише Preferences
// namespace "hist", щоб не дублювати вже наявні loadHistoryRetentionDays()/
// /settings/history/save в UPS_Monitor.ino. Впливає лише на фільтр читання
// (historyOldestValidTs), НЕ на розмір файлів (вони завжди розраховані на
// HISTORY_RETENTION_DAYS_MAX).
uint16_t g_historyRetentionDays = HISTORY_RETENTION_DAYS_DEFAULT;

// Викликати з /settings/history/save ОДРАЗУ ПІСЛЯ того, як там же
// збережено значення в Preferences - тут лише оновлюється значення в RAM,
// яким користується historyOldestValidTs() при читанні.
void historySetRetentionDays(uint16_t days) {
  if (days < 1) days = 1;
  if (days > HISTORY_RETENTION_DAYS_MAX) days = HISTORY_RETENTION_DAYS_MAX;
  g_historyRetentionDays = days;
}

// retentionDays - поточне значення з Preferences (з loadHistoryRetentionDays()
// у UPS_Monitor.ino), щоб не читати той самий namespace двічі з різних місць.
bool historyBegin(int retentionDays) {
  historySetRetentionDays(retentionDays);

  if (!FFat.begin(true)) { // true = відформатувати, якщо розділ ще не FAT/пошкоджений
    Serial.println("[HIST] Помилка монтування FFat");
    return false;
  }

  bool ok1 = histDetail.begin(HISTORY_DETAIL_PATH, HISTORY_DETAIL_CAPACITY);
  bool ok2 = histLong.begin(HISTORY_LONG_PATH, HISTORY_LONG_CAPACITY);

  Serial.printf("[HIST] FFat: %llu/%llu KB використано\n",
                (unsigned long long)(FFat.usedBytes() / 1024), (unsigned long long)(FFat.totalBytes() / 1024));
  Serial.printf("[HIST] detail=%s (%lu записів, %.0f KB), long=%s (%lu записів, %.0f KB), retention=%u днів\n",
                ok1 ? "OK" : "FAIL", (unsigned long)HISTORY_DETAIL_CAPACITY, histDetail.fileSizeBytes() / 1024.0,
                ok2 ? "OK" : "FAIL", (unsigned long)HISTORY_LONG_CAPACITY, histLong.fileSizeBytes() / 1024.0,
                g_historyRetentionDays);
  return ok1 && ok2;
}

uint32_t historyOldestValidTs() {
  return (uint32_t)(time(nullptr) - (uint32_t)g_historyRetentionDays * 86400UL);
}

// Викликати з того самого місця, де вже оновлюється cachedReadings (з
// частотою SAMPLE_INTERVAL_MS) - НЕ окремим таймером, щоб не читати INA3221
// вдруге. Приклад виклику показано в UPS_Monitor.ino нижче.
void historyOnSample(const Reading& batt, const Reading& psu, const Reading& ups, float socPercent) {
  if (!historyTimeSynced()) return; // без синхронізованого часу мітки будуть сміттям - пропускаємо запис

  g_detailAccum.add(batt, psu, ups, socPercent);
  g_longAccum.add(batt, psu, ups, socPercent);

  uint32_t now = (uint32_t)time(nullptr);

  if (g_lastDetailFlush == 0) g_lastDetailFlush = now; // перший виклик після синхронізації часу - не пишемо "недо-хвилинний" запис одразу
  if (now - g_lastDetailFlush >= HISTORY_DETAIL_INTERVAL_S) {
    HistSample s;
    if (g_detailAccum.toSample(s, now)) histDetail.append(s);
    g_detailAccum.reset();
    g_lastDetailFlush = now;
  }

  if (g_lastLongFlush == 0) g_lastLongFlush = now;
  if (now - g_lastLongFlush >= HISTORY_LONG_INTERVAL_S) {
    HistSample s;
    if (g_longAccum.toSample(s, now)) histLong.append(s);
    g_longAccum.reset();
    g_lastLongFlush = now;
  }
}

// ---------- API для графіків ----------
// Основна функція - приймає АБСОЛЮТНІ межі діапазону (fromTs/toTs, unix-час),
// а не "скільки секунд углиб від зараз". Це дозволяє запитувати довільний
// період у минулому (напр. "позавчора з 14:00 до 18:00"), а не лише
// "останні N годин від поточного моменту" - useDetail нижче керується саме
// відстанню fromTs від "зараз", тож старий та новий спосіб виклику вибирають
// рівень деталізації однаково.
//
// desiredPoints - бажана кількість точок (сервер сам проріджує дані так,
// щоб віддати приблизно стільки, незалежно від фактичної роздільності
// збереження - клієнту не треба знати про detail/long рівні взагалі).
//
// Обирає рівень автоматично: якщо fromTs потрапляє у вікно detail-рівня
// (HISTORY_DETAIL_DAYS від "зараз") - береться detail (1 хв), інакше -
// long (15 хв). Потужність по кожному каналу рахується тут з V*I - не
// зберігається окремо (див. коментар до HistSample вище).
String historyQueryJsonRange(uint32_t fromTs, uint32_t toTs, uint32_t desiredPoints) {
  if (desiredPoints < 10) desiredPoints = 10;
  if (desiredPoints > 2000) desiredPoints = 2000;

  uint32_t now = (uint32_t)time(nullptr);
  if (toTs > now) toTs = now;       // не показуємо майбутнє
  if (fromTs > toTs) fromTs = toTs; // захист від переплутаного/некоректного запиту

  uint32_t oldestValid = historyOldestValidTs();
  if (fromTs < oldestValid) fromTs = oldestValid; // не показуємо те, що вже "поза" обраним retention

  bool useDetail = (now - fromTs) <= (HISTORY_DETAIL_DAYS * 86400UL);
  HistRing& ring = useDetail ? histDetail : histLong;
  uint32_t intervalS = useDetail ? HISTORY_DETAIL_INTERVAL_S : HISTORY_LONG_INTERVAL_S;

  uint32_t totalPossible = (toTs > fromTs) ? ((toTs - fromTs) / intervalS) + 1 : 1;
  uint32_t stride = (totalPossible > desiredPoints) ? (totalPossible / desiredPoints) : 1;
  if (stride == 0) stride = 1;

  static HistSample buf[2000]; // капнуто вище під desiredPoints<=2000
  size_t n = ring.queryChronological(fromTs, toTs, stride, buf, desiredPoints);

  JsonDocument doc;
  doc["from"] = fromTs;
  doc["to"] = toTs;
  doc["resolution_s"] = intervalS * stride;
  doc["tier"] = useDetail ? "detail" : "long";
  JsonArray points = doc["points"].to<JsonArray>();
  for (size_t i = 0; i < n; i++) {
    float bV = buf[i].battV_cV / 100.0f, bI = buf[i].battI_mA;
    float pV = buf[i].psuV_cV  / 100.0f, pI = buf[i].psuI_mA;
    float uV = buf[i].upsV_cV  / 100.0f, uI = buf[i].upsI_mA;

    JsonObject p = points.add<JsonObject>();
    p["t"]   = buf[i].ts;
    p["bV"]  = serialized(String(bV, 2));
    p["bI"]  = (int)bI;
    p["bP"]  = serialized(String(bV * bI / 1000.0f, 1));
    p["pV"]  = serialized(String(pV, 2));
    p["pI"]  = (int)pI;
    p["pP"]  = serialized(String(pV * pI / 1000.0f, 1));
    p["uV"]  = serialized(String(uV, 2));
    p["uI"]  = (int)uI;
    p["uP"]  = serialized(String(uV * uI / 1000.0f, 1));
    p["soc"] = buf[i].socPct;
  }

  String json;
  serializeJson(doc, json);
  return json;
}

// Зручна обгортка "rangeSeconds секунд углиб від зараз" - лишається заради
// сумісності викликів hours=/days= (див. UPS_Monitor.ino).
String historyQueryJson(uint32_t rangeSeconds, uint32_t desiredPoints) {
  uint32_t now = (uint32_t)time(nullptr);
  uint32_t fromTs = (now > rangeSeconds) ? (now - rangeSeconds) : 0;
  return historyQueryJsonRange(fromTs, now, desiredPoints);
}

// Текстовий дебаг-звіт (сирі значення, без проріджування) - швидко
// перевірити стан сховища; для самих графіків використовується
// historyQueryJson() вище.
String historyDebugText() {
  String out;
  out += "FFat: " + String(FFat.usedBytes() / 1024) + " / " + String(FFat.totalBytes() / 1024) + " KB\n";
  out += "time synced: " + String(historyTimeSynced() ? "yes" : "no") + "\n";
  out += "retention: " + String(g_historyRetentionDays) + " days\n";
  out += "detail: capacity=" + String(HISTORY_DETAIL_CAPACITY) + " interval=" + String(HISTORY_DETAIL_INTERVAL_S) + "s size=" + String(histDetail.fileSizeBytes() / 1024) + "KB\n";
  out += "long:   capacity=" + String(HISTORY_LONG_CAPACITY) + " interval=" + String(HISTORY_LONG_INTERVAL_S) + "s size=" + String(histLong.fileSizeBytes() / 1024) + "KB\n";

  HistSample buf[5];
  size_t n = histDetail.queryChronological(historyOldestValidTs(), 0xFFFFFFFF, 1, buf, 5);
  out += "detail sample count (first 5 found): " + String(n) + "\n";
  for (size_t i = 0; i < n; i++) {
    out += "  ts=" + String(buf[i].ts) + " battV=" + String(buf[i].battV_cV / 100.0, 2) +
           " battI=" + String(buf[i].battI_mA) + " soc=" + String(buf[i].socPct) + "\n";
  }
  return out;
}
