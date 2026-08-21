#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Button.h>
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

// --- Buttons ---------------------------------------------------------------
volatile bool nxtPress = false;
volatile bool prvPress = false;
volatile bool selPress = false;
volatile bool escPress = false;
static void onBtnANextCb(void *button_handle, void *usr_data) { nxtPress = true; }
static void onBtnASelCb(void *button_handle, void *usr_data) { selPress = true; }
static void onBtnBPrevCb(void *button_handle, void *usr_data) { prvPress = true; }
static void onBtnBEscCb(void *button_handle, void *usr_data) { escPress = true; }

Button *btnA;
Button *btnB;

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

    button_config_t cfgA = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 500,
        .short_press_time = 120,
        .gpio_button_config = {
                               .gpio_num = BTN_A_PIN,
                               .active_level = 0,
                               },
    };
    button_config_t cfgB = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 500,
        .short_press_time = 120,
        .gpio_button_config = {
                               .gpio_num = BTN_B_PIN,
                               .active_level = 0,
                               },
    };

    btnA = new Button(cfgA);
    btnA->attachSingleClickEventCb(&onBtnANextCb, NULL);
    btnA->attachLongPressStartEventCb(&onBtnASelCb, NULL);

    btnB = new Button(cfgB);
    btnB->attachSingleClickEventCb(&onBtnBPrevCb, NULL);
    btnB->attachLongPressStartEventCb(&onBtnBEscCb, NULL);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);

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
void _setBrightness(uint8_t brightval) {
    int dutyCycle = (brightval * 255) / 100;
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

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
void InputHandler(void) {
    static unsigned long tm = launcherMillis();
    static bool btn_pressed = false;
    if (nxtPress || prvPress || selPress || escPress) btn_pressed = true;

    if (launcherMillis() - tm > 200 || LongPress) {
        if (btn_pressed) {
            btn_pressed = false;
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;
            NextPress = nxtPress;
            PrevPress = prvPress;
            SelPress = selPress;
            EscPress = escPress;

            nxtPress = false;
            prvPress = false;
            selPress = false;
            escPress = false;
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { pm1.shutdown(); }
