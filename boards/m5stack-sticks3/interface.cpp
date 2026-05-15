#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_Bits 8
#define TFT_BRIGHT_FREQ 5000

constexpr uint32_t kDwDoublePressWindowMs = 250;
constexpr uint32_t kDwLongPressMs = 600;
constexpr uint32_t kDwDebounceMs = 8;

static volatile uint32_t dw_last_isr_ms = 0;
static volatile uint32_t dw_press_ms = 0;
static volatile uint32_t dw_first_release_ms = 0;
static volatile bool dw_is_down = false;
static volatile bool dw_waiting = false;
static volatile bool dw_double_ready = false;
static volatile bool dw_long_seen = false;

void IRAM_ATTR isr_dw_btn() {
    uint32_t now = launcherMillis();
    if (now - dw_last_isr_ms < kDwDebounceMs) return;
    dw_last_isr_ms = now;
    bool pressed = (launcherGpioRead(DW_BTN) == BTN_ACT);
    if (pressed) {
        dw_is_down = true;
        dw_press_ms = now;
        return;
    }

    dw_is_down = false;
    if (dw_long_seen) {
        dw_long_seen = false;
        dw_waiting = false;
        return;
    }

    if ((now - dw_press_ms) < kDwLongPressMs) {
        if (dw_waiting && (now - dw_first_release_ms) <= kDwDoublePressWindowMs) {
            dw_double_ready = true;
            dw_waiting = false;
        } else {
            dw_waiting = true;
            dw_first_release_ms = now;
        }
    } else {
        dw_waiting = false;
    }
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    M5.begin();
    M5.Power.setExtOutput(false);
    // Disable 5V output to external port
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

    launcherGpioInputPullup(SEL_BTN);
    launcherGpioInputPullup(DW_BTN);
    attachInterrupt(digitalPinToInterrupt(DW_BTN), isr_dw_btn, CHANGE);
}
/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // PWM backlight setup
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);
}
/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 250;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 5;
    else dutyCycle = ((brightval * 250) / 100);

    // launcherConsolePrintf("dutyCycle for bright 0-255: %d\n", dutyCycle);

    vTaskDelay(10 / portTICK_PERIOD_MS);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        // launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
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
    static int lastState = -1;
    bool charging = M5.Power.isCharging();
    if (charging && lastState != 1) {
        lastState = 1;
        M5.Power.setExtOutput(false);
    } else if (!charging && lastState != 0) {
        lastState = 0;
        M5.Power.setExtOutput(true);
    }
    int level = M5.Power.getBatteryLevel();
    return (level < 0) ? 0 : (level >= 100) ? 100 : level;
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    static bool dwLongFired = false;
    unsigned long now = launcherMillis();
    if (now - tm < 200 && !LongPress) return;
    if (!wakeUpScreen()) AnyKeyPress = true;
    else return;

    bool selPressed = (launcherGpioRead(SEL_BTN) == BTN_ACT);
    bool dwPressed = dw_is_down;
    bool dwWaiting = dw_waiting;
    bool dwDoubleReady = dw_double_ready;
    unsigned long dwPressStart = dw_press_ms;
    unsigned long dwFirstRelease = dw_first_release_ms;

    AnyKeyPress = selPressed || dwPressed || dwWaiting || dwDoubleReady;

    if (selPressed) {
        SelPress = true;
        tm = now;
    }

    if (dwPressed) {
        if (!dwLongFired && (now - dwPressStart) > kDwLongPressMs) {
            PrevPress = true;
            dwLongFired = true;
            dw_waiting = false;
            dw_double_ready = false;
            dw_long_seen = true;
            tm = now;
        }
    } else if (dwLongFired) {
        dwLongFired = false;
    }

    if (dwDoubleReady) {
        PrevPress = true;
        dw_double_ready = false;
        dw_waiting = false;
        tm = now;
    } else if (dwWaiting && !dwPressed && (now - dwFirstRelease) > kDwDoublePressWindowMs) {
        NextPress = true;
        dw_waiting = false;
        tm = now;
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { M5.Power.powerOff(); }
