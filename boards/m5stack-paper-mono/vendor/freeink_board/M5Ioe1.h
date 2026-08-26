#pragma once

// M5Stack Paper Mono — M5IOE1 I/O expander (PY32L020), single owner.
//
// The Paper Mono routes most of its power/reset plumbing through this expander
// on the system I2C bus (SDA47/SCL48, shared with the M5PM1 PMIC at 0x6E, the
// RX8130 RTC and the FT6336 touch controller): EPD power + reset, touch power +
// reset, TF-card power, PDM-mic power and the green/blue legs of the RGB LED.
// Like the PM1 it is a PY32 MCU emulating an I2C slave in firmware, so every
// transaction needs the same ~500 us pacing (reuse m5pm1::XFER_DELAY_US).
//
// NOTE: the register map is the official m5stack/M5IOE1 one and is DIFFERENT
// from the PM1's (GPIO mode lives at 0x03/0x04 here, not 0x10; 0x13 is drive
// config low byte, not the PM1's GPIO_DRV). Registers are 16-bit L/H pairs; bit
// N of the 16-bit value = expander pin "IO(N+1)" (schematic PYG(N+1)).
//
// The device answers at 0x6F or 0x4F depending on its ADD_SET strap (official
// schematic says 0x4F, the crosspoint-reader-mono bring-up saw 0x6F) — begin()
// probes both and caches whichever responds.

#include <Arduino.h>
#include <Wire.h>

#include "M5Pm1.h"

namespace freeink {
namespace m5ioe1 {

constexpr uint8_t ADDR_PRIMARY = 0x6F;
constexpr uint8_t ADDR_ALT = 0x4F;

// --- registers (official m5stack/M5IOE1 src/M5IOE1.h) ---
constexpr uint8_t REG_UID_L = 0x00;
constexpr uint8_t REG_GPIO_MODE_L = 0x03;  // 1 = output
constexpr uint8_t REG_GPIO_OUT_L = 0x05;   // output level
constexpr uint8_t REG_GPIO_IN_L = 0x07;    // input level
constexpr uint8_t REG_GPIO_PU_L = 0x09;    // pull-up enable
constexpr uint8_t REG_GPIO_PD_L = 0x0B;    // pull-down enable
constexpr uint8_t REG_GPIO_DRV_L = 0x13;   // 0 = push-pull, 1 = open-drain
constexpr uint8_t REG_I2C_CFG = 0x23;      // [4]SPD(1=400k) [3:0]idle-sleep timeout; 0 = stay awake
// PWM duty pairs (L then H; H byte: [7]EN [6]POL [3:0]duty high). PWM1 drives
// IO9 (blue LED leg), PWM2 drives IO8 (green) — the pins' alt function.
constexpr uint8_t REG_PWM1_DUTY_L = 0x1B;
constexpr uint8_t REG_PWM2_DUTY_L = 0x1D;

// --- Paper Mono pin assignments (schematic U17; bit index = IO number - 1) ---
constexpr uint16_t PIN_TF_DETECT = 1u << 0;    // IO1  PYB_TF_DET (input, pulled up)
constexpr uint16_t PIN_EPD_POWER = 1u << 2;    // IO3  PYB_EPD_EN (EPD_3V3_L3B rail, active high)
constexpr uint16_t PIN_EPD_RESET = 1u << 4;    // IO5  PYB_EINK_RST
constexpr uint16_t PIN_TOUCH_RESET = 1u << 5;  // IO6  PYB_TP_RST
constexpr uint16_t PIN_LED_GREEN = 1u << 7;    // IO8  PYB_LED_G (also PWM ch2)
constexpr uint16_t PIN_LED_BLUE = 1u << 8;     // IO9  PYB_LED_B (also PWM ch1)
constexpr uint16_t PIN_MIC_POWER = 1u << 11;   // IO12 PYB_PDM_EN (PDM_VDD rail)
constexpr uint16_t PIN_TOUCH_POWER = 1u << 12; // IO13 PYB_TP_EN (TP_VDD rail + I2C switch)
constexpr uint16_t PIN_SD_POWER = 1u << 13;    // IO14 PYB_TF_EN (TF_3V3_L3B rail)

// Everything the firmware drives as an output. TF_DETECT stays an input.
constexpr uint16_t OUTPUT_MASK = PIN_EPD_POWER | PIN_EPD_RESET | PIN_TOUCH_RESET | PIN_LED_GREEN |
                                 PIN_LED_BLUE | PIN_MIC_POWER | PIN_TOUCH_POWER | PIN_SD_POWER;

// Cached probe result: 0 = not probed yet, 0xFF = probed and absent.
inline uint8_t g_addr = 0;

inline bool writeBytesAt(uint8_t addr, uint8_t reg, const uint8_t* data, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  for (uint8_t i = 0; i < len; ++i) Wire.write(data[i]);
  if (Wire.endTransmission() != 0) return false;
  delayMicroseconds(m5pm1::XFER_DELAY_US);
  return true;
}

inline bool readBytesAt(uint8_t addr, uint8_t reg, uint8_t* data, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  delayMicroseconds(m5pm1::XFER_DELAY_US);
  if (Wire.requestFrom(addr, len) != len) return false;
  for (uint8_t i = 0; i < len; ++i) data[i] = Wire.read();
  delayMicroseconds(m5pm1::XFER_DELAY_US);
  return true;
}

// Probe 0x6F then 0x4F (UID readable at 0x00) and cache the responding address.
// Callers must have run m5pm1::beginBus() (same physical bus) first; begin() does.
inline bool begin() {
  if (g_addr != 0 && g_addr != 0xFF) return true;
  if (g_addr == 0xFF) return false;
  m5pm1::beginBus();
  for (uint8_t addr : {ADDR_PRIMARY, ADDR_ALT}) {
    uint8_t uid[2] = {0, 0};
    if (readBytesAt(addr, REG_UID_L, uid, sizeof(uid))) {
      g_addr = addr;
      // Full speed awake: 100 kHz, never idle-sleep (matches the PM1 policy —
      // an idle-slept PY32 slave serves garbage on the first transaction).
      const uint8_t cfg = 0x00;
      writeBytesAt(g_addr, REG_I2C_CFG, &cfg, 1);
      return true;
    }
  }
  g_addr = 0xFF;
  return false;
}

inline bool present() { return begin(); }

inline bool writeReg16(uint8_t regL, uint16_t value) {
  if (!begin()) return false;
  const uint8_t data[2] = {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)};
  return writeBytesAt(g_addr, regL, data, sizeof(data));
}

inline bool readReg16(uint8_t regL, uint16_t* value) {
  if (!begin()) return false;
  uint8_t data[2] = {0, 0};
  if (!readBytesAt(g_addr, regL, data, sizeof(data))) return false;
  *value = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  return true;
}

inline bool updateReg16(uint8_t regL, uint16_t clearMask, uint16_t setMask) {
  uint16_t value = 0;
  if (!readReg16(regL, &value)) return false;
  return writeReg16(regL, static_cast<uint16_t>((value & ~clearMask) | setMask));
}

// Configure OUTPUT_MASK pins as push-pull outputs driving LOW (rails off, resets
// asserted), with pulls off; TF_DETECT gets its pull-up. The consumer board
// bring-up then raises the rails it needs.
inline bool configureOutputs() {
  if (!begin()) return false;
  // IO8/IO9 (green/blue LED legs) double as the PY32's PWM2/PWM1 outputs, and
  // the stock firmware drives blue through PWM1 as its status light. Like the
  // PM1's PWR_CFG, the PWM engine state is retained across reflashes — while
  // a channel is enabled it owns the pin and the GPIO writes below never
  // reach it. Disable both channels (duty 0, EN bit clear).
  static constexpr uint8_t pwmOff[2] = {0x00, 0x00};
  bool ok = writeBytesAt(g_addr, REG_PWM1_DUTY_L, pwmOff, sizeof(pwmOff));
  ok &= writeBytesAt(g_addr, REG_PWM2_DUTY_L, pwmOff, sizeof(pwmOff));
  ok &= updateReg16(REG_GPIO_OUT_L, OUTPUT_MASK, 0);
  ok &= updateReg16(REG_GPIO_PU_L, OUTPUT_MASK, PIN_TF_DETECT);
  ok &= updateReg16(REG_GPIO_PD_L, OUTPUT_MASK | PIN_TF_DETECT, 0);
  ok &= updateReg16(REG_GPIO_DRV_L, OUTPUT_MASK, 0);
  ok &= updateReg16(REG_GPIO_MODE_L, PIN_TF_DETECT, OUTPUT_MASK);
  return ok;
}

inline bool write(uint16_t pinMask, bool high) {
  return updateReg16(REG_GPIO_OUT_L, high ? 0 : pinMask, high ? pinMask : 0);
}

inline bool read(uint16_t pinMask, bool* high) {
  uint16_t value = 0;
  if (!readReg16(REG_GPIO_IN_L, &value)) return false;
  *high = (value & pinMask) != 0;
  return true;
}

}  // namespace m5ioe1
}  // namespace freeink
