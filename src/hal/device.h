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
