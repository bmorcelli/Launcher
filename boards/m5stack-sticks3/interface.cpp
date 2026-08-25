#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5PM1.h>
#include <Wire.h>
#include <interface.h>
#ifdef USE_CARDKB2
#include <cardkb2.h>
#endif

#define BTN_A_PIN 11
#define BTN_B_PIN 12

// --- M5PM1 power-management IC ------------------------------------------
#define PM1_SDA 47
#define PM1_SCL 48

static M5PM1 pm1;

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    if (pm1.begin(&Wire1, M5PM1_DEFAULT_ADDR, PM1_SDA, PM1_SCL) != M5PM1_OK) {
        launcherConsolePrintf("%s\n", String("M5PM1 init failed").c_str());
    }
    pm1.setChargeEnable(true);
    launcherDelayMs(20);
    pm1.setDcdcEnable(true);
    launcherDelayMs(20);
    pm1.setLdoEnable(true);
    launcherDelayMs(20);

    pm1.gpioSetFunc(M5PM1_GPIO_NUM_2, M5PM1_GPIO_FUNC_GPIO);
    pm1.gpioSetMode(M5PM1_GPIO_NUM_2, M5PM1_GPIO_MODE_OUTPUT);
    pm1.gpioSetDrive(M5PM1_GPIO_NUM_2, M5PM1_GPIO_DRIVE_PUSHPULL);
    pm1.gpioSetOutput(M5PM1_GPIO_NUM_2, HIGH);
    launcherDelayMs(20);

#ifndef USE_CARDKB2
    pm1.setBoostEnable(false);
#else
    pm1.setBoostEnable(true); // CardKB2 needs Grove 5V, energized directly at boot
    delay(100);
#endif
    /*
  | Device  | SCK   | MISO  | MOSI  | CS    | GDO0/CE   |
  | ---     | :---: | :---: | :---: | :---: | :---:     |
  | SD Card | 5     | 4     | 6     | 7     | ---       |
  | CC1101  | 5     | 4     | 6     | 2     | 3         |
  | NRF24   | 5     | 4     | 6     | 8     | 1         |
  | PN532   | 5     | 4     | 6     | 43    | --        |
  | WS500   | 5     | 4     | 6     | **    | **        |
  | LoRa    | 5     | 4     | 6     | **    | **        |
      */
    launcherGpioOutput(7);
    launcherGpioWrite(7, HIGH); // SD Card CS
    launcherGpioOutput(2);
    launcherGpioWrite(2, HIGH); // CC1101 CS
    launcherGpioOutput(8);
    launcherGpioWrite(8, HIGH); // nRF24L01 CS
    launcherGpioOutput(43);
    launcherGpioWrite(43, HIGH); // PN532 CS
    launcherGpioOutput(9);
    launcherGpioWrite(9, LOW); // M5RF433 avoid Jamming
    launcherGpioOutput(46);
    launcherGpioWrite(46, LOW); // Infrared LED Off

    hal_buttons_init_2(DeviceButtons{BTN_A_PIN, BTN_B_PIN}, 600);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);

#ifdef USE_CARDKB2
    // CardKB2 on the Grove port (G9/G10). Probing reconfigures G9 as I2C SDA,
    // so restore the RF433 anti-jam state if no keyboard is attached.
    if (!CardKB2Installed) {
        pm1.setBoostEnable(false);
        launcherGpioOutput(9);
        launcherGpioWrite(9, LOW); // M5RF433 avoid Jamming
    }
#endif
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    uint16_t mv = 0;
    if (pm1.readVbat(&mv) != M5PM1_OK) return 0;
    int level = (int)(((float)mv - 3300.0f) * 100.0f / (4150.0f - 3350.0f));
    return (level < 0) ? 0 : (level >= 100) ? 100 : level;
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) { hal_buttons_poll_2(); }

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { pm1.shutdown(); }
