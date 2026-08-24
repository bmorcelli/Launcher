#ifndef LAUNCHER_HAL_INPUTS_TOUCH_H
#define LAUNCHER_HAL_INPUTS_TOUCH_H

#include "../device.h"

// Touchscreen HAL -- driver selected by build_flags macro (mirrors
// PMIC_*/GAUGE_*): TOUCH_CTRL_XPT2046 (CYD28_TouchscreenR, SPI, resistive);
// TOUCH_CTRL_GT911, TOUCH_CTRL_CST8XX or TOUCH_CTRL_FT6X36 (SensorLib
// TouchDrvGT911 / TouchDrvCSTXXX / TouchDrvFT6X36, all I2C capacitive).
// TouchDrvFT6X36 covers the whole FT6206/FT6236/FT6336(U)/FT3267/FT5336/
// FT3068 family, so FT6336 panels use TOUCH_CTRL_FT6X36 too -- there is no
// separate "FT6336" driver.
//
// cfg.MirrorX/MirrorY/SwapXY are indexed by the display's current rotation
// (0-3) and applied to the raw touch point before it's handed back, exactly
// replicating the per-rotation coordinate math every touch board used to
// duplicate in its own InputHandler(). cfg.pin_sda/pin_scl/pin_rst/pin_irq
// only matter for the I2C drivers (GT911/CST8XX/FT6X36); ignored for
// TOUCH_CTRL_XPT2046, whose pins are compile-time macros from
// CYD28_TouchscreenR.h (CYD28_TouchR_*, set via build_flags). cfg.i2c_bus
// (void* to a TwoWire, nullptr = the global `Wire`) is for a board whose
// touch controller sits on a second I2C bus -- e.g. lilygo-t-watch-s3,
// where `Wire` is already the sensor/PMIC bus and touch is on `Wire1`.

struct LTouchPoint; // include/globals.h -- only referenced by pointer/ref here

// Configures the selected touch controller.
//
// TOUCH_CTRL_GT911: i2c_addr defaults to GT911_SLAVE_ADDRESS_L (0x5D); pass
// 0x14 (GT911_SLAVE_ADDRESS_H) if the board wires ADDR high. xpt_shared_spi
// is ignored.
//
// TOUCH_CTRL_CST8XX: i2c_addr is the panel's primary address (boards differ
// -- CST328_SLAVE_ADDRESS 0x1A, CST816_SLAVE_ADDRESS 0x15, CST226SE_SLAVE_
// ADDRESS 0x5A, all from <TouchDrvCSTXXX.hpp> -- pass the right one, there
// is no single correct default). If that address doesn't answer, a second
// try at CST816_SLAVE_ADDRESS is attempted automatically (some panels
// identify as either depending on batch, see lilygo-t-display-s3-touch).
// xpt_shared_spi is ignored.
//
// TOUCH_CTRL_FT6X36: i2c_addr and xpt_shared_spi are ignored, always
// FT6X36_SLAVE_ADDRESS (0x38, every chip this driver covers shares it).
// Logs the detected chip's model name, then calls touch.interruptPolling()
// (required before polled reads, see hal_touch_read()'s doc).
//
// TOUCH_CTRL_XPT2046: i2c_addr is ignored. xpt_shared_spi (default true)
// picks which CYD28_TouchR::begin() overload is used: true passes &SPI, so
// the touch controller reuses the already-running hardware SPI bus (every
// known XPT2046 board does this -- if the bus needs its own SPI.begin()
// first because the panel isn't itself on SPI, e.g. lilygo-t-hmi's parallel
// TFT, call that from the board's own _setup_gpio()/_post_setup_gpio()
// before hal_touch_init()). Pass false only for a board whose touch
// controller is wired to different pins than the shared SPI bus and must
// bit-bang its own (e.g. CYD-2432S028's default/XPT2046 branch).
bool hal_touch_init(const DeviceTouch &cfg, uint8_t i2c_addr = 0x5D, bool xpt_shared_spi = true);

// Polls the controller once. The screen size used for the mirror math (and,
// TOUCH_CTRL_GT911/TOUCH_CTRL_CST8XX only, setMaxCoordinates()) is computed
// internally from displayConfig.width/height (the physical panel's native
// resolution) swapped per the current rotation -- NOT tftWidth/tftHeight,
// which on a HAS_TOUCH board without HAS_TOUCH_NO_BORDER are shrunk by the
// footer nav-hint strip (main.cpp/settings.cpp's `- (_fm * LH + 4)`). The
// touch controller has no notion of that footer -- its coordinate range is
// always the full panel -- so using tftHeight there silently offset every
// mirrored axis by the footer's height. Returns false if nothing is pressed
// right now (out left untouched); true with out.x/out.y/out.pressed filled
// in otherwise.
bool hal_touch_read(const DeviceTouch &cfg, LTouchPoint &out);

// TOUCH_CTRL_CST8XX only: registers the panel's virtual "home button" touch
// zone + callback (vendor API passthrough -- touch.setCenterButtonCoordinate
// + touch.setHomeButtonCallback -- left as a passthrough rather than folded
// into the HAL because what the callback actually does varies per board:
// some set EscPress, one toggles the backlight directly). No-op for other
// drivers.
void hal_touch_set_home_button(int16_t x, int16_t y, void (*cb)(void *user_data), void *user_data = nullptr);

// TOUCH_CTRL_CST8XX only: touch.disableAutoSleep() passthrough -- needed
// when polling (as hal_touch_read() does) instead of using the panel's IRQ
// line, per the vendor's own docs, else the chip can go to sleep mid-poll
// and every read after that errors out. No-op for other drivers.
void hal_touch_disable_auto_sleep();

// TOUCH_CTRL_FT6X36 only: touch.setThreshold() passthrough -- raises/lowers
// the panel's touch-detect sensitivity register (chip default ~22; some
// boards raise it to reduce phantom touches inside a case). Call only if a
// board actually needs a non-default value -- leaving it uncalled keeps the
// chip's own default. No-op for other drivers.
void hal_touch_set_threshold(uint8_t value);

// Common tail every board's InputHandler() used to duplicate after a
// successful hal_touch_read(): wake the screen (this press is swallowed as
// the wake-up tap, same convention as every other input source), then
// publish the point to the global touchPoint/touchHeatMap(). Returns false
// if the caller should return immediately from InputHandler() (the screen
// was asleep and this call is what just woke it -- AnyKeyPress is already
// set); true if the point was applied and the caller can keep going. Pass
// wakeUp = false to skip the wakeUpScreen() gate entirely (point is always
// applied, AnyKeyPress always set, return value always true) -- only for a
// board whose touch controller must not be treated as a wake-up source.
bool hal_touch_apply(const LTouchPoint &t, bool wakeUp = true);

#endif
