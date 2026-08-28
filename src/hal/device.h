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
    bool pullup = true; // false for boards without internal/external pull-ups (e.g. m5stack-cplus2)
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
