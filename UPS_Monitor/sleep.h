#pragma once
#include <WiFi.h>
#include <esp_sleep.h>
#include "driver/rtc_io.h"
#include "config.h"
#include "ina3221.h"

// Кеш показань і сам сенсор - визначені в головному .ino файлі.
extern Reading cachedReadings[3];
extern INA3221 ina;

int findChannelByLabel(const char* label); // визначена в nut_server.h

// Форсоване збереження стану coulomb-каунтера, що інакше чекало б на свій
// періодичний таймер (COULOMB_SAVE_INTERVAL_MS, див. nut_server.h) - deep
// sleep для чипа рівнозначний повній перезагрузці (RAM не зберігається,
// крім RTC_DATA_ATTR), тож усе, що не встигло зафлешитись у Preferences,
// буде втрачено. Історія (history.h) цього НЕ потребує - там кожен запис
// вже й так синхронно флашиться в append().
void saveCoulombState();  // nut_server.h

// nowMs, коли безперервно триває умова "немає напруги на PSU in і на UPS
// out" - 0, якщо умова зараз не виконується. Перевіряється в loop().
uint32_t sleepConditionSinceMs = 0;

// true - якщо це пробудження було саме "підстрахувальним" таймерним
// (ESP_SLEEP_WAKEUP_TIMER), а не апаратним PV-сигналом чи звичайним
// холодним стартом - впливає на дебаунс нижче І на "швидкий шлях" у
// setup() головного .ino (без WiFi, якщо умова й досі виконується).
bool wokeFromTimerRecheck = false;

// Миттєвий (без дебаунсу) знімок умови "немає напруги на PSU in і на UPS
// out" за поточним cachedReadings. Викликати ЛИШЕ після forceSample()/
// sampleReadings() - інакше зчитає нульові/застарілі покази.
bool sleepConditionNow() {
  int psuIdx = findChannelByLabel("PSU in");
  int outIdx = findChannelByLabel("UPS out");
  if (psuIdx < 0 || outIdx < 0) return false;
  return (cachedReadings[psuIdx].busVoltage_V < SLEEP_VOLTAGE_THRESHOLD_V) &&
         (cachedReadings[outIdx].busVoltage_V < SLEEP_VOLTAGE_THRESHOLD_V);
}

// Викликати один раз із setup(), одразу після ina.begin() - записує
// апаратні пороги Power-Valid у сам чип.
void sleepSetupPowerValid() {
  ina.setPowerValidLimits(SLEEP_PV_UPPER_V, SLEEP_PV_LOWER_V);
}

// Викликати один раз із setup(), одразу після Serial.begin() - визначає й
// логує причину поточного завантаження, і запам'ятовує, чи це було
// "підстрахувальне" таймерне пробудження (для короткого дебаунсу нижче).
void sleepLogWakeReason() {
  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
  switch (reason) {
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Прокинулись: INA3221 Power-Valid (GPIO4) - PSU і UPS out, схоже, повернулись одночасно");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Прокинулись: підстрахувальна періодична перевірка (таймер сну)");
      wokeFromTimerRecheck = true;
      break;
    default:
      Serial.println("Звичайний старт (не після сну)");
      break;
  }
}

// Заходить у deep sleep з обома джерелами пробудження одразу армованими:
// (1) апаратний PV на GPIO4 - швидкий шлях для типового випадку, коли PSU
//     і UPS out повертаються практично одночасно;
// (2) таймер (SLEEP_TIMER_RECHECK_US) - підстраховує "АБО"-логіку, якої
//     сам PV апаратно не вміє (див. пояснення в config.h).
// Не повертається - виконання продовжується з setup() після пробудження,
// так само як після звичайної перезагрузки.
void sleepEnterDeepSleep() {
  Serial.println("Живлення відсутнє на PSU in і на UPS out - засинаю...");

  saveCoulombState();

  // R21 (10K до VS) на платі вже дає зовнішній pull-up на PV - не боремось
  // з ним внутрішніми резисторами ESP32, просто вимикаємо обидва.
  rtc_gpio_pullup_dis(SLEEP_PV_GPIO);
  rtc_gpio_pulldown_dis(SLEEP_PV_GPIO);
  esp_sleep_enable_ext1_wakeup((1ULL << SLEEP_PV_GPIO), ESP_EXT1_WAKEUP_ANY_HIGH);

  esp_sleep_enable_timer_wakeup(SLEEP_TIMER_RECHECK_US);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  Serial.flush();
  esp_deep_sleep_start();
}

// Викликати з loop(), після sampleReadings() - оцінює умову сну за свіжими
// показами і, якщо вона протрималась достатньо довго (дебаунс залежить від
// того, чи це підстрахувальне таймерне пробудження - див. config.h),
// заходить у сон.
void sleepCheckCondition() {
  bool bothAbsent = sleepConditionNow();
  uint32_t nowMs = millis();

  if (!bothAbsent) {
    sleepConditionSinceMs = 0; // умова перервалась (щось з'явилось) - скидаємо очікування
    return;
  }
  if (sleepConditionSinceMs == 0) {
    sleepConditionSinceMs = nowMs; // умова щойно почалась виконуватись
    return;
  }

  uint32_t debounceMs = wokeFromTimerRecheck ? SLEEP_TIMER_RECHECK_DEBOUNCE_MS : SLEEP_DEBOUNCE_MS;
  if (nowMs - sleepConditionSinceMs >= debounceMs) {
    sleepEnterDeepSleep();
  }
}
