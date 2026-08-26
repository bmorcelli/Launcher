#pragma once

// M5Stack Paper Mono — board bring-up over the PY32 helper chips, single owner.
//
// The Paper Mono routes its power/reset plumbing through the M5PM1 PMIC
// (M5Pm1.h) and the M5IOE1 expander (M5Ioe1.h) instead of ESP GPIOs. This
// header is the one place that sequences them, so the SDK's hardware managers
// can bring the board up themselves and consumer firmware never touches the
// PMIC/expander directly:
//   - EpdBus calls setEpdPower()/setEpdReset() (the profile leaves the display
//     powerEnable/rst pins unassigned),
//   - InputManager calls enableTouch() before probing the FT6336 and polls
//     pollPowerButtonClicked() into BTN_POWER events,
//   - LedManager / FrontlightManager already talk to the PM1/IOE1 via their
//     own configs.
// Everything funnels through ensureBooted(), so whichever manager runs first
// establishes the boot state (same idempotent-init pattern as the PaperColor's
// m5pm1::applyBootPowerPolicy()).
//
// Header-only and PIN-compatible with nothing else: only compile-time
// papermono paths include it.

#include <Arduino.h>

#include "M5Ioe1.h"
#include "M5Pm1.h"

namespace freeink {
namespace papermono {

// One-time boot bring-up (idempotent, cheap after the first call): PMIC bus up
// with watchdog off, wake source cleared, power button taken over (the PMIC's
// single-click reset is disabled so clicks reach the firmware; its hardware
// long-hold download escape stays), and all expander-switched rails/resets
// driven to a known LOW. Rails come up individually as each manager needs them.
inline bool ensureBooted() {
  static bool booted = false;
  static bool ok = false;
  if (booted) return ok;
  booted = true;
  m5pm1::beginBus();
  m5pm1::clearWakeSource();
  m5pm1::configureAppPowerButton();
  // The red RGB leg lives in the PM1's PWR_CFG, which the PMIC retains across
  // reflashes — stock firmware leaves it lit. Drive it to the same known-off
  // state configureOutputs() gives the IOE1 green/blue legs.
  m5pm1::updateReg(m5pm1::REG_PWR_CFG, m5pm1::LED_R_EN, 0);
  ok = m5ioe1::configureOutputs();
  return ok;
}

// EPD rail (IOE1 IO3 -> EPD_3V3_L3B LDO). Power-on settle is EpdBus's own
// 100 ms delay; power-off gets the reference bring-up's 10 ms.
inline void setEpdPower(bool enabled) {
  ensureBooted();
  m5ioe1::write(m5ioe1::PIN_EPD_POWER, enabled);
  if (!enabled) delay(10);
}

inline void setEpdReset(bool high) { m5ioe1::write(m5ioe1::PIN_EPD_RESET, high); }

// TF-card rail (IOE1 IO14 PYB_TF_EN -> TF_3V3_L3B LDO). configureOutputs()
// boots it LOW, so the card is unpowered until SDCardManager raises it before
// the SDMMC mount. 10 ms settle on power-up so the rail is stable before the
// first CMD0 (same as the touch rail's bring-up).
inline void setSdPower(bool enabled) {
  ensureBooted();
  m5ioe1::write(m5ioe1::PIN_SD_POWER, enabled);
  if (enabled) delay(10);
}

// Touch rail up + reset released so the FT6336 answers its I2C probe. Same
// 8 ms low / 10 ms settle as the reference bring-up.
inline void enableTouch() {
  ensureBooted();
  m5ioe1::write(m5ioe1::PIN_TOUCH_POWER, true);
  m5ioe1::write(m5ioe1::PIN_TOUCH_RESET, false);
  delay(8);
  m5ioe1::write(m5ioe1::PIN_TOUCH_RESET, true);
  delay(10);
}

// Poll the PM1's power button (the event bit is read-clear in the PMIC).
// Returns true once per click. Rate-limited internally: each poll costs two
// paced I2C transactions to a firmware-emulated slave (~2 ms), so per-tick
// polling from update() would be a tax on every loop.
inline bool pollPowerButtonClicked(unsigned long nowMs) {
  static unsigned long lastPollMs = 0;
  constexpr unsigned long POLL_INTERVAL_MS = 250;
  if (nowMs - lastPollMs < POLL_INTERVAL_MS) return false;
  lastPollMs = nowMs;
  if (!ensureBooted()) return false;
  uint8_t status = 0;
  if (!m5pm1::readButtonStatus(&status)) return false;
  return (status & m5pm1::BTN_EVENT) != 0;
}

}  // namespace papermono
}  // namespace freeink
