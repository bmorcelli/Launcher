#ifndef LAUNCHER_HAL_INPUTS_TOUCH_H
#define LAUNCHER_HAL_INPUTS_TOUCH_H

#include "../device.h"

// Touchscreen HAL -- driver selected by build_flags macro (mirrors
// PMIC_*/GAUGE_*): TOUCH_CTRL_XPT2046 (CYD28_TouchscreenR, SPI, resistive)
// or TOUCH_CTRL_GT911 (SensorLib TouchDrvGT911, I2C, capacitive).
//
// cfg.MirrorX/MirrorY/SwapXY are indexed by the display's current rotation
// (0-3) and applied to the raw touch point before it's handed back, exactly
// replicating the per-rotation coordinate math every touch board used to
// duplicate in its own InputHandler(). cfg.pin_sda/pin_scl/pin_rst/pin_irq
// only matter for TOUCH_CTRL_GT911 (I2C bus + reset/irq pins); ignored for
// TOUCH_CTRL_XPT2046, whose pins are compile-time macros from
// CYD28_TouchscreenR.h (CYD28_TouchR_*, set via build_flags).

struct LTouchPoint; // include/globals.h -- only referenced by pointer/ref here

// Configures the selected touch controller.
//
// TOUCH_CTRL_GT911: i2c_addr defaults to GT911_SLAVE_ADDRESS_L (0x5D); pass
// 0x14 (GT911_SLAVE_ADDRESS_H) if the board wires ADDR high. xpt_shared_spi
// is ignored.
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

// Polls the controller once. screenW/screenH must be the display's *current*
// (already rotated) width/height -- same tftWidth/tftHeight every board
// already used in its own mirror math -- used both for the mirror math here
// and (TOUCH_CTRL_GT911 only) setMaxCoordinates(). Returns false if nothing
// is pressed right now (out left untouched); true with out.x/out.y/out.pressed
// filled in otherwise.
bool hal_touch_read(const DeviceTouch &cfg, int16_t screenW, int16_t screenH, LTouchPoint &out);

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
