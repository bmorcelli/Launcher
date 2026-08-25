#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <GaugeAXP2602.hpp>
#include <SD_MMC.h>
#include <Wire.h>
#include <interface.h>
#define SEL_BTN 0
#define DW_BTN 28
#define BTN_ACT LOW

GaugeAXP2602 gauge;

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    // DW_BTN -> Next (single click) / Sel (600ms hold)
    // SEL_BTN -> Prev (single click) / Esc (600ms hold)
    hal_buttons_init_2(DeviceButtons{DW_BTN, SEL_BTN}, 600);

    if (!gauge.begin(Wire, 2, 3)) { Serial.println("Failed to AXP2602 - check your wiring!"); }
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // PWM backlight setup
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);
}

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

int getBattery() {
    if (gauge.refresh()) {
        return gauge.getStateOfCharge();
    } else {
        return -1;
    }
}

void InputHandler(void) { hal_buttons_poll_2(); }
