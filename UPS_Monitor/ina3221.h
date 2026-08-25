#pragma once
#include <Wire.h>
#include <math.h>
#include "config.h"

// INA3221 register addresses
#define REG_CONFIG      0x00
#define REG_CH1_SHUNT   0x01
#define REG_CH1_BUS     0x02
// CH2 = REG_CH1_x + 2, CH3 = REG_CH1_x + 4
#define REG_MASK_ENABLE 0x0F

// Config: CH1-3 enabled, AVG=64, VBUS CT=1.1ms, VSH CT=1.1ms, continuous shunt+bus
// (higher averaging than default to smooth the offset jitter we saw at idle;
// full cycle ≈ 64 * (1.1+1.1)ms * 3ch ≈ 420ms, well under the 1s poll interval)
#define INA3221_CONFIG  0x7727

struct Reading {
  float busVoltage_V;
  float shuntVoltage_mV;
  float current_mA;
};

class INA3221 {
public:
  bool begin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.beginTransmission(INA3221_ADDR);
    if (Wire.endTransmission() != 0) return false; // device not responding
    writeReg(REG_CONFIG, INA3221_CONFIG);
    return true;
  }

  // channel: 1..3
  Reading read(uint8_t channel) {
    uint8_t shuntReg = REG_CH1_SHUNT + (channel - 1) * 2;
    uint8_t busReg   = REG_CH1_BUS   + (channel - 1) * 2;

    int16_t rawShunt = readReg(shuntReg);
    int16_t rawBus   = readReg(busReg);

    // Both values are 13-bit signed, left-justified in bits 15..3
    float shunt_mV = (rawShunt >> 3) * 0.040f;   // 40 uV/LSB
    float bus_V    = (rawBus   >> 3) * 0.008f;   // 8 mV/LSB

    const ChannelCal& cal = CH_CAL[channel - 1];
    float correctedShunt_mV = shunt_mV - cal.offset_mV;
    float current_mA = (correctedShunt_mV / cal.rShunt_mOhm) * 1000.0f;

    if (fabsf(current_mA) < CURRENT_DEADBAND_MA) {
      current_mA = 0.0f;
    }

    Reading r;
    r.busVoltage_V = bus_V;
    r.shuntVoltage_mV = shunt_mV;
    r.current_mA = current_mA;
    return r;
  }

private:
  void writeReg(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(INA3221_ADDR);
    Wire.write(reg);
    Wire.write(value >> 8);
    Wire.write(value & 0xFF);
    Wire.endTransmission();
  }

  int16_t readReg(uint8_t reg) {
    Wire.beginTransmission(INA3221_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((int)INA3221_ADDR, 2);
    uint16_t val = 0;
    if (Wire.available() >= 2) {
      val = (Wire.read() << 8) | Wire.read();
    }
    return (int16_t)val;
  }
};
