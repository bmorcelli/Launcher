#include "touch.h"

#include "globals.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"

#if defined(TOUCH_CTRL_XPT2046)
#include "CYD28_TouchscreenR.h"
#include <SPI.h>
#ifndef CYD28_DISPLAY_HOR_RES_MAX
#define CYD28_DISPLAY_HOR_RES_MAX 320
#endif
#ifndef CYD28_DISPLAY_VER_RES_MAX
#define CYD28_DISPLAY_VER_RES_MAX 240
#endif
// Named `touch` (external linkage, not `static`) on purpose: settings.cpp's
// calibrateTouch()/loadTouchCalibration() (HAS_RESISTIVE_TOUCH) still do
// `extern CYD28_TouchR touch;` to reach the raw setTouch()/touched()/
// getPointRaw() calibration API, same as every un-migrated XPT2046 board's
// own interface.cpp used to provide. Keep this name until every
// TOUCH_CTRL_XPT2046 board is migrated and that calibration code is updated
// to go through a HAL entry point instead.
CYD28_TouchR touch(CYD28_DISPLAY_HOR_RES_MAX, CYD28_DISPLAY_VER_RES_MAX);

#elif defined(TOUCH_CTRL_GT911)
#include <TouchDrvGT911.hpp>
#include <Wire.h>
static TouchDrvGT911 touch;
static uint8_t touchLastRot = 0xFF;
#endif

bool hal_touch_init(const DeviceTouch &cfg, uint8_t i2c_addr, bool xpt_shared_spi) {
#if defined(TOUCH_CTRL_XPT2046)
    (void)cfg;
    (void)i2c_addr;
    return xpt_shared_spi ? touch.begin(&SPI) : touch.begin();
#elif defined(TOUCH_CTRL_GT911)
    (void)xpt_shared_spi;
    if (cfg.pin_sda >= 0 && cfg.pin_scl >= 0) Wire.begin(cfg.pin_sda, cfg.pin_scl);
    touch.setPins(cfg.pin_rst, cfg.pin_irq);
    return touch.begin(Wire, i2c_addr);
#else
    (void)cfg;
    (void)i2c_addr;
    (void)xpt_shared_spi;
    return false;
#endif
}

bool hal_touch_read(const DeviceTouch &cfg, int16_t screenW, int16_t screenH, LTouchPoint &out) {
#if defined(TOUCH_CTRL_XPT2046) || defined(TOUCH_CTRL_GT911)
    uint8_t r = rotation & 0x03;
#endif
#if defined(TOUCH_CTRL_XPT2046)
    if (!touch.touched()) return false;
    auto p = touch.getPointScaled();
    int16_t x = p.x, y = p.y;
    if (cfg.SwapXY[r]) {
        int16_t t = x;
        x = y;
        y = t;
    }
    if (cfg.MirrorX[r]) x = screenW - x;
    if (cfg.MirrorY[r]) y = screenH - y;
    out.x = x;
    out.y = y;
    out.pressed = true;
    return true;
#elif defined(TOUCH_CTRL_GT911)
    if (touchLastRot != r) {
        touch.setMaxCoordinates(screenW, screenH);
        touch.setSwapXY(cfg.SwapXY[r]);
        touch.setMirrorXY(cfg.MirrorX[r], cfg.MirrorY[r]);
        touchLastRot = r;
    }
    int16_t x = 0, y = 0;
    if (!touch.getPoint(&x, &y, 1)) return false;
    out.x = x;
    out.y = y;
    out.pressed = true;
    return true;
#else
    (void)cfg;
    (void)screenW;
    (void)screenH;
    (void)out;
    return false;
#endif
}

bool hal_touch_apply(const LTouchPoint &t, bool wakeUp) {
    if (wakeUp) {
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return false;
    } else {
        AnyKeyPress = true;
    }

    touchPoint.x = t.x;
    touchPoint.y = t.y;
    touchPoint.pressed = true;
    touchHeatMap(touchPoint);
    return true;
}
