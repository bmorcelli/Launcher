#ifndef LAUNCHER_HAL_DEVICE_H
#define LAUNCHER_HAL_DEVICE_H

#include <cstdint>

// Pure data describing a board's input/power wiring. Filled by each
// board's _setup_gpio() and handed to the hal_* modules; no logic here.

struct DeviceButtons {
    int8_t btn1 = -1;
    int8_t btn2 = -1;
    int8_t btn3 = -1;
    int8_t btn4 = -1;
    int8_t btn5 = -1;
    int8_t btn6 = -1;
    bool pullup = true;      // false for boards without internal/external pull-ups (e.g. m5stack-cplus2)
    bool activeHigh = false; // true for boards wired active-HIGH (e.g. seeedstudio-reterminal-d1001)
};

struct DeviceTouch {
    int8_t pin_sda = -1;
    int8_t pin_scl = -1;
    int8_t pin_rst = -1;
    int8_t pin_irq = -1;
    bool MirrorX[4] = {false, false, false, false};
    bool MirrorY[4] = {false, false, false, false};
    bool SwapXY[4] = {false, false, false, false};
    int16_t HomeBtn = -1;
    void *i2c_bus = nullptr;
    uint16_t raw_width = 0;
    uint16_t raw_height = 0;
    bool gt911_int_sync = false;
    // Drives the touch controller's RST line on boards where it isn't a raw
    // ESP32 GPIO (e.g. behind an IO expander like M5IOE1) -- called with
    // HIGH/LOW instead of the pin_rst GPIO writes hal_touch_init would
    // otherwise do. Leave pin_rst at -1 when this is set; hal_touch_init
    // pulses low then high through the callback before the chip driver's
    // begin(), and passes -1 as its own pin_rst so it never touches a raw
    // GPIO itself.
    void (*reset_cb)(bool level) = nullptr;
    // TOUCH_CTRL_CST8XX only: the shared driver (SensorLib's TouchDrvCSTXXX)
    // auto-probes chip families in this fixed order: 0=CST226, 1=CST8XX
    // (CST816/CST820/CST716), 2=CST92xx, 3=CST3530. Fine for boards that
    // don't know their exact chip, but every failed candidate ahead of the
    // real one still pulses RST (with that candidate's own, possibly wrong,
    // timing) and writes/reads its own register map on the real chip before
    // the correct driver ever runs -- which can leave it reporting a
    // stuck/phantom touch. Set this to the real chip's index (see above) on
    // boards that already know it, to skip straight to that driver. -1 (the
    // default) leaves auto-probing enabled.
    int8_t cst8xx_model = -1;
};

struct DeviceEncoder {
    int8_t pin_a = -1;
    int8_t pin_b = -1;
    int8_t pin_sel = -1;
    int8_t pin_esc = -1; // -1 if there's no dedicated esc button (only the encoder + pin_sel)
    bool pullup = false; // internal pull-up on pin_sel/pin_esc
};

struct DevicePmic {
    int8_t pin_sda = -1;
    int8_t pin_scl = -1;
    uint8_t address = 0;
};

struct DeviceGauge {
    int8_t pin_sda = -1;
    int8_t pin_scl = -1;
    uint8_t address = 0;
    uint16_t design_capacity_mah = 0;
};

#endif
