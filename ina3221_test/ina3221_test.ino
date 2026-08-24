/*
 * Тестовий скетч: читання INA3221 (3 канали) на ESP32-S3
 * Виводить напругу шини, напругу на шунті та розрахований струм для кожного каналу.
 *
 * Підключення (згідно узгодженої схеми):
 *   INA3221 VS  -> 3V3 (ESP32-S3)
 *   INA3221 GND -> GND (ESP32-S3)
 *   INA3221 SDA -> GPIO8
 *   INA3221 SCL -> GPIO9
 *   INA3221 A0  -> GND (адреса 0x40)
 *
 * Бібліотека не потрібна - працюємо напряму з регістрами через Wire.
 */

#include <Wire.h>

#define I2C_SDA_PIN      8
#define I2C_SCL_PIN      9
#define INA3221_ADDR     0x40   // A0 -> GND

// Регістри INA3221 (див. даташит TI INA3221)
#define REG_CONFIG       0x00
#define REG_CH1_SHUNT    0x01
#define REG_CH1_BUS      0x02
#define REG_CH2_SHUNT    0x03
#define REG_CH2_BUS      0x04
#define REG_CH3_SHUNT    0x05
#define REG_CH3_BUS      0x06
#define REG_MANUF_ID     0xFE
#define REG_DIE_ID       0xFF

// Значення конфігурації: reset=0, усі 3 канали увімкнені,
// averaging=4 семпли, VBUS CT=1.1мс, VSH CT=1.1мс, режим = безперервний shunt+bus
#define CONFIG_VALUE     0x7127

// Опір шунтів у Омах - відкалібровано за ДВОМА точками струму кожного каналу
// для розділення нахилу (R) та офсету нуля.
const float R_SHUNT[3] = {0.008947, 0.009050, 0.008966}; // CH1, CH2, CH3 - усі відкалібровано

// Офсет напруги шунта у Вольтах (систематичне зміщення нуля, виявлене при
// калібруванні за двома точками - без нього похибка зростає на малих струмах)
const float OFFSET_V[3] = {0.000294, 0.000315, 0.000322}; // CH1, CH2, CH3 - усі відкалібровано

// LSB згідно даташиту (регістри лівойюстовані, 13-біт значення у бітах [15:3])
const float SHUNT_LSB_V = 0.00004;  // 40 мкВ на біт
const float BUS_LSB_V   = 0.008;    // 8 мВ на біт

bool writeReg16(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(INA3221_ADDR);
  Wire.write(reg);
  Wire.write((value >> 8) & 0xFF);
  Wire.write(value & 0xFF);
  return Wire.endTransmission() == 0;
}

bool readReg16(uint8_t reg, int16_t &value) {
  Wire.beginTransmission(INA3221_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((uint8_t)INA3221_ADDR, (uint8_t)2) != 2) return false;
  uint16_t raw = (Wire.read() << 8) | Wire.read();
  value = (int16_t)raw;
  return true;
}

// Перетворює сирий регістр (13-біт значення у старших бітах) у вольти
float rawToVolts(int16_t raw, float lsb) {
  return (raw >> 3) * lsb;
}

void printChannel(uint8_t idx, uint8_t shuntReg, uint8_t busReg, float rShunt, float offsetV) {
  int16_t rawShunt, rawBus;
  bool okShunt = readReg16(shuntReg, rawShunt);
  bool okBus   = readReg16(busReg, rawBus);

  if (!okShunt || !okBus) {
    Serial.printf("CH%d: помилка читання I2C\n", idx);
    return;
  }

  float vShunt = rawToVolts(rawShunt, SHUNT_LSB_V);           // Вольти (сирий сигнал)
  float vBus   = rawToVolts(rawBus, BUS_LSB_V);                // Вольти
  float current_mA = ((vShunt - offsetV) / rShunt) * 1000.0;   // мА, з компенсацією офсету

  Serial.printf("CH%d: Uшини = %.3f В | Uшунта = %.2f мВ | I = %.1f мА\n",
                idx, vBus, vShunt * 1000.0, current_mA);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== INA3221 test ===");

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000); // стартуємо на 100кГц для надійності на макетній платі

  // Перевірка присутності пристрою на шині
  Wire.beginTransmission(INA3221_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("ПОМИЛКА: INA3221 не відповідає за адресою 0x40!");
    Serial.println("Перевірте SDA/SCL, живлення VS, підтяжки та адресу A0.");
  } else {
    Serial.println("INA3221 знайдено на шині I2C (0x40).");
  }

  // Читаємо Manufacturer ID / Die ID для додаткової перевірки (має бути 0x5449 / 0x3220)
  int16_t manufId, dieId;
  if (readReg16(REG_MANUF_ID, manufId) && readReg16(REG_DIE_ID, dieId)) {
    Serial.printf("Manufacturer ID: 0x%04X (очікується 0x5449)\n", (uint16_t)manufId);
    Serial.printf("Die ID:          0x%04X (очікується 0x3220 або 0x3221)\n", (uint16_t)dieId);
  }

  // Записуємо конфігурацію
  if (writeReg16(REG_CONFIG, CONFIG_VALUE)) {
    Serial.println("Конфігурацію записано успішно.");
  } else {
    Serial.println("ПОМИЛКА запису конфігурації!");
  }

  Serial.println("--------------------------------------");
}

void loop() {
  printChannel(1, REG_CH1_SHUNT, REG_CH1_BUS, R_SHUNT[0], OFFSET_V[0]);
  printChannel(2, REG_CH2_SHUNT, REG_CH2_BUS, R_SHUNT[1], OFFSET_V[1]);
  printChannel(3, REG_CH3_SHUNT, REG_CH3_BUS, R_SHUNT[2], OFFSET_V[2]);
  Serial.println("--------------------------------------");

  delay(1000);
}
