#pragma once

// M5Stack PaperColor — M5PM1 power-management IC (PY32L020), single owner.
//
// Two FreeInk modules drive the same PMIC over the board's internal I2C bus
// (SDA3/SCL2): the ED2208 display driver (EPD rail via GPIO0, boot power policy)
// and LedManager (the 3.3V LDO that feeds the WS2812 RGB rail). They share one
// physical register — PWR_CFG (0x06) — so they must NOT keep private copies of
// its map: an earlier split had LedManager mislabel reg 0x09 as a "watchdog"
// (it's I2C_CFG) and the two callers' notions of the power bits drifted apart.
//
// This header is the single source of truth: one register map, one bus init, one
// place that expresses the board's boot power policy. Header-only (tiny inline
// I2C RMW) so both libs pick it up via their existing BoardConfig dependency with
// no extra lib wiring.

#include <Arduino.h>
#include <Wire.h>

namespace freeink {
namespace m5pm1 {

constexpr uint8_t ADDR = 0x6E;

// Internal I2C bus (PMIC + co-resident peripherals). PaperColor: SDA3/SCL2.
// Paper Mono: SDA47/SCL48 (G47_SYS_SDA/G48_SYS_SCL, shared with the IOE1,
// RX8130 RTC and FT6336 touch); its GPIO2/3 are KEY1/KEY2, so the PaperColor
// default there would put Wire on button pins and kill the whole bus.
#ifndef FREEINK_M5PM1_SDA
#if FREEINK_DEVICE_PAPERMONO
#define FREEINK_M5PM1_SDA 47
#else
#define FREEINK_M5PM1_SDA 3
#endif
#endif
#ifndef FREEINK_M5PM1_SCL
#if FREEINK_DEVICE_PAPERMONO
#define FREEINK_M5PM1_SCL 48
#else
#define FREEINK_M5PM1_SCL 2
#endif
#endif
constexpr int SDA = FREEINK_M5PM1_SDA;
constexpr int SCL = FREEINK_M5PM1_SCL;
constexpr uint32_t I2C_HZ = 100000;

// --- registers (official M5PM1 datasheet) ---
constexpr uint8_t REG_PWR_CFG = 0x06;     // [3]BOOST_EN [2]LDO_EN [1]DCDC_EN [0]CHG_EN
                                          // auto-clears to 0 on reset/download/shutdown
constexpr uint8_t REG_WAKE_SRC = 0x05;    // latched wake source; write 0 to clear
constexpr uint8_t REG_I2C_CFG = 0x09;     // [4]SPD(0=100k) [3:0]SLP_TO (0=no idle sleep)
constexpr uint8_t REG_WDT_CNT = 0x0A;     // seconds; 0 disables the PMIC watchdog
constexpr uint8_t REG_SYS_CMD = 0x0C;     // KEY=0xA in high nibble; 1=off, 2=reset, 3=download
constexpr uint8_t REG_GPIO_MODE = 0x10;   // 1 = output
constexpr uint8_t REG_GPIO_OUT = 0x11;    // output level
constexpr uint8_t REG_GPIO_DRV = 0x13;    // 0 = push-pull
constexpr uint8_t REG_GPIO_FUNC0 = 0x16;  // 0 = plain GPIO (vs alt function)
constexpr uint8_t REG_PWM0_DUTY_L = 0x30;
constexpr uint8_t REG_PWM_FREQ_L = 0x34;
constexpr uint8_t REG_BTN_STATUS = 0x48;  // [7] event (read-clear), [0] physical level
constexpr uint8_t REG_BTN_CFG_1 = 0x49;   // [7] DL lock, [6:1] timing, [0] disable click reset
constexpr uint8_t REG_BTN_CFG_2 = 0x4A;   // [0] disable double-click power-off
constexpr uint8_t REG_NEO_CFG = 0x50;     // PM1's own NeoPixel engine: [6]refresh [5:0]count
                                          // 0x00 = M5PM1::disableLeds(): engine off, ESP owns chain

// --- PWR_CFG (0x06) bits ---
constexpr uint8_t CHG_EN = 1 << 0;    // battery charge enable
constexpr uint8_t DCDC_EN = 1 << 1;   // 5V DCDC
constexpr uint8_t LDO_EN = 1 << 2;    // 3.3V LDO = WS2812 RGB rail (+ green indicator LED)
constexpr uint8_t BOOST_EN = 1 << 3;  // 5VINOUT / Grove boost
constexpr uint8_t LED_R_EN = 1 << 4;  // Paper Mono: red leg of the discrete RGB LED

constexpr uint8_t WAKE_PWR_BUTTON = 1 << 2;
constexpr uint8_t BTN_EVENT = 1 << 7;
constexpr uint8_t BTN_PRESSED = 1 << 0;
constexpr uint8_t BTN_DOWNLOAD_LOCK = 1 << 7;
constexpr uint8_t BTN_SINGLE_RESET_DISABLE = 1 << 0;
constexpr uint8_t SYS_CMD_SHUTDOWN = 0xA1;

// EPD_EN is wired to PMIC GPIO0.
constexpr uint8_t GPIO0 = 1 << 0;

// Init the internal I2C bus and pin the PM1 to 100 kHz with idle-sleep disabled
// (SLP_TO=0) so it doesn't drop off the bus between transactions. Idempotent —
// safe to call from each module's begin().
inline void beginBus() {
  Wire.begin(SDA, SCL, I2C_HZ);
  Wire.setTimeOut(4);
  Wire.beginTransmission(ADDR);
  Wire.write(REG_I2C_CFG);
  Wire.write(0x00);
  Wire.endTransmission();
  delayMicroseconds(500);
  Wire.beginTransmission(ADDR);
  Wire.write(REG_WDT_CNT);
  Wire.write(0x00);
  Wire.endTransmission();
}

// The PM1 is a PY32 MCU emulating an I2C slave in firmware, not a hardware
// register file. M5's driver pads every transaction with 500 µs and reads
// multi-byte values as one burst; without the pause after the pointer write
// the slave serves stale data (e.g. VBAT reading 150 mV), and without burst
// reads the two halves of a 16-bit value come from different sample windows.
constexpr uint32_t XFER_DELAY_US = 500;

inline bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(value);
  if (Wire.endTransmission() != 0) return false;
  delayMicroseconds(XFER_DELAY_US);
  return true;
}

inline bool writeBytes(uint8_t reg, const uint8_t* data, uint8_t len) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  for (uint8_t i = 0; i < len; ++i) Wire.write(data[i]);
  if (Wire.endTransmission() != 0) return false;
  delayMicroseconds(XFER_DELAY_US);
  return true;
}

inline bool writeReg16(uint8_t reg, uint16_t value) {
  const uint8_t data[2] = {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)};
  return writeBytes(reg, data, sizeof(data));
}

inline bool readBytes(uint8_t reg, uint8_t* data, uint8_t len) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  delayMicroseconds(XFER_DELAY_US);
  if (Wire.requestFrom(ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; ++i) data[i] = Wire.read();
  return true;
}

inline bool readReg(uint8_t reg, uint8_t* value) { return readBytes(reg, value, 1); }

// 16-bit little-endian burst read (voltage/ADC result registers).
inline bool readReg16(uint8_t reg, uint16_t* value) {
  uint8_t buf[2];
  if (!readBytes(reg, buf, 2)) return false;
  *value = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
  return true;
}

inline bool updateReg(uint8_t reg, uint8_t clearMask, uint8_t setMask) {
  uint8_t value = 0;
  if (!readReg(reg, &value)) return false;
  return writeReg(reg, static_cast<uint8_t>((value & ~clearMask) | setMask));
}

inline bool configureAppPowerButton(uint8_t* configuredValue = nullptr) {
  uint8_t value = 0;
  if (!readReg(REG_BTN_CFG_1, &value)) return false;
  // Keep the timing fields intact. Only take over the single-click reset and
  // explicitly leave the PMIC's hardware long-hold download path enabled.
  value = static_cast<uint8_t>((value | BTN_SINGLE_RESET_DISABLE) & ~BTN_DOWNLOAD_LOCK);
  if (!writeReg(REG_BTN_CFG_1, value)) return false;
  uint8_t verify = 0;
  if (!readReg(REG_BTN_CFG_1, &verify)) return false;
  if (configuredValue) *configuredValue = verify;
  return (verify & (BTN_SINGLE_RESET_DISABLE | BTN_DOWNLOAD_LOCK)) == BTN_SINGLE_RESET_DISABLE;
}

inline bool readButtonStatus(uint8_t* value) { return readReg(REG_BTN_STATUS, value); }

inline bool readWakeSource(uint8_t* value) { return readReg(REG_WAKE_SRC, value); }

inline bool clearWakeSource() { return writeReg(REG_WAKE_SRC, 0x00); }

inline bool requestShutdown() {
  // The PMIC library's reference implementation waits before issuing a system
  // command so any preceding rail writes have reached the PY32 firmware.
  delay(120);
  return writeReg(REG_SYS_CMD, SYS_CMD_SHUTDOWN);
}

// Board boot power policy. PWR_CFG auto-clears on reset, so this is where the
// board's standing power state is (re)established each boot:
//   CHG_EN  SET   — the PM1 only charges the 1250 mAh cell when this is on, and
//                   regulates the curve itself (charges only when VIN present,
//                   stops at full). Off by default after reset; without setting
//                   it the battery never tops up over USB.
//   DCDC_EN SET   — the 5 V DCDC is the system 5 V rail the EPD drive runs from.
//                   With it off the panel refreshes undervolted and sags blue —
//                   on battery AND on USB. The PM1 retains PWR_CFG across
//                   reflashes, so a once-latched-off DCDC stays off until
//                   something sets it again; assert it every boot.
//   BOOST_EN SET  — Grove/5VINOUT supply; M5's UserDemo boots with
//                   setBoostEnable(true), match it.
//   LDO_EN  CLEAR — RGB rail, owned by LedManager (re-enabled lazily while an LED
//                   is lit). Cleared here so the chain stays unpowered from boot.
// Also hands the WS2812 chain to the ESP by switching off the PM1's built-in
// NeoPixel engine — left on, it renders its own status pixel (the stuck green LED
// seen at boot) even while the ESP sleeps. The display is the first PM1 caller,
// so doing both here kills the green LED before LedManager has even run.
inline void applyBootPowerPolicy() {
  updateReg(REG_PWR_CFG, LDO_EN, CHG_EN | DCDC_EN | BOOST_EN);
  writeReg(REG_NEO_CFG, 0x00);
}

// Silence the PM1's NeoPixel engine so the ESP owns the WS2812 chain.
inline void disableLeds() { writeReg(REG_NEO_CFG, 0x00); }

// The 3.3V LDO RGB rail, owned by LedManager (on only while an LED is lit).
inline void setRgbRail(bool on) { updateReg(REG_PWR_CFG, on ? 0 : LDO_EN, on ? LDO_EN : 0); }

}  // namespace m5pm1
}  // namespace freeink
