#include "TouchDrvGT911.hpp"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>
#include "idf/launcher_platform.h"
TouchDrvGT911 touch;

struct LTouchPointPro {
    int16_t x = 0;
    int16_t y = 0;
};

// Setup for Trackball
void IRAM_ATTR ISR_up();
void IRAM_ATTR ISR_down();
void IRAM_ATTR ISR_left();
void IRAM_ATTR ISR_right();

volatile int8_t trackball_axis_x = 0;
volatile int8_t trackball_axis_y = 0;
volatile uint32_t trackball_axis_expiry_ms = 0;

#define TRACKBALL_AXIS_COOLDOWN_MS 250
#define TRACKBALL_AXIS_THRESHOLD 2

void IRAM_ATTR ISR_up() {
    trackball_axis_y > 0 ? trackball_axis_y = -1 : --trackball_axis_y;
    trackball_axis_expiry_ms = launcherMillis() + TRACKBALL_AXIS_COOLDOWN_MS;
}
void IRAM_ATTR ISR_down() {
    trackball_axis_y < 0 ? trackball_axis_y = 1 : ++trackball_axis_y;
    trackball_axis_expiry_ms = launcherMillis() + TRACKBALL_AXIS_COOLDOWN_MS;
}
void IRAM_ATTR ISR_left() {
    trackball_axis_x > 0 ? trackball_axis_x = -1 : --trackball_axis_x;
    trackball_axis_expiry_ms = launcherMillis() + TRACKBALL_AXIS_COOLDOWN_MS;
}
void IRAM_ATTR ISR_right() {
    trackball_axis_x < 0 ? trackball_axis_x = 1 : ++trackball_axis_x;
    trackball_axis_expiry_ms = launcherMillis() + TRACKBALL_AXIS_COOLDOWN_MS;
}

void ISR_rst() {
    trackball_axis_x = 0;
    trackball_axis_y = 0;
    trackball_axis_expiry_ms = 0;
}

#define LILYGO_KB_SLAVE_ADDRESS 0x55
#define LILYGO_KB_BRIGHTNESS_CMD 0x01
#define KB_I2C_SDA 18
#define KB_I2C_SCL 8
#define SEL_BTN 0
#define UP_BTN 3
#define DW_BTN 15
#define L_BTN 1
#define R_BTN 2
#define PIN_POWER_ON 10
#define BOARD_TOUCH_INT 16

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherDelayMs(500); // time to ESP32C3 start and enable the keyboard
    if (!Wire.begin(KB_I2C_SDA, KB_I2C_SCL)) launcherConsolePrintf("%s\n", String("Fail starting ESP32-C3 keyboard").c_str());

    launcherGpioOutput(PIN_POWER_ON);
    launcherGpioWrite(PIN_POWER_ON, HIGH);
    launcherGpioInput(SEL_BTN);
    launcherGpioInput(BOARD_TOUCH_INT);
    touch.setPins(-1, BOARD_TOUCH_INT);
    if (!touch.begin(Wire, GT911_SLAVE_ADDRESS_L)) {
        launcherConsolePrintf("%s\n", String("Failed to find GT911 - check your wiring!").c_str());
    }

    launcherGpioOutput(9); // LoRa Radio CS Pin to HIGH (Inhibit the SPI Communication for this module)
    launcherGpioWrite(9, HIGH);

    // Setup for Trackball
    launcherGpioInputPullup(UP_BTN);
    attachInterrupt(UP_BTN, ISR_up, FALLING);
    launcherGpioInputPullup(DW_BTN);
    attachInterrupt(DW_BTN, ISR_down, FALLING);
    launcherGpioInputPullup(L_BTN);
    attachInterrupt(L_BTN, ISR_left, FALLING);
    launcherGpioInputPullup(R_BTN);
    attachInterrupt(R_BTN, ISR_right, FALLING);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
// uint8_t isPlus = false;
void _post_setup_gpio() {}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    Wire.beginTransmission(LILYGO_KB_SLAVE_ADDRESS);
    Wire.write(LILYGO_KB_BRIGHTNESS_CMD);
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
        Wire.write(brightval);
    } else {
        int bl = MINBRIGHT + round(((255 - MINBRIGHT) * brightval / 100));
        analogWrite(TFT_BL, bl);
        Wire.write(bl);
    }
    Wire.endTransmission();
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    char keyValue = 0;
    static unsigned long tm = launcherMillis();
    LTouchPointPro t;
    uint8_t touched = 0;
    uint8_t rot = 5;
#ifdef T_DECK_PLUS
    bool isPlus = true;
#else
    bool isPlus = false;
#endif
    if (rot != rotation) {
        if (rotation == 1) {
            touch.setMaxCoordinates(320, 240);
            touch.setSwapXY(true);
            touch.setMirrorXY(!isPlus, true);
        }
        if (rotation == 3) {
            touch.setMaxCoordinates(320, 240);
            touch.setSwapXY(true);
            touch.setMirrorXY(isPlus, false);
        }
        if (rotation == 0) {
            touch.setMaxCoordinates(240, 320);
            touch.setSwapXY(false);
            touch.setMirrorXY(false, !isPlus);
        }
        if (rotation == 2) {
            touch.setMaxCoordinates(240, 320);
            touch.setSwapXY(false);
            touch.setMirrorXY(true, isPlus);
        }
        rot = rotation;
    }
    touched = touch.getPoint(&t.x, &t.y, 1);
    launcherDelayMs(1);
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    while (Wire.available() > 0) {
        keyValue = Wire.read();
        launcherDelayMs(1);
    }
    if (launcherMillis() - tm < 200 && !LongPress) return;

    // if the trackball movement has expired, reset it to avoid unwanted movements
    if (trackball_axis_expiry_ms && trackball_axis_expiry_ms <= launcherMillis()) { ISR_rst(); }

    if (abs(trackball_axis_x) >= TRACKBALL_AXIS_THRESHOLD ||
        abs(trackball_axis_y) >= TRACKBALL_AXIS_THRESHOLD) {

        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;

        // launcherConsolePrintf("%s", String("Trackball: [").c_str());
        // launcherConsolePrintf("%s", String(trackball_axis_x).c_str()); launcherConsolePrintf("%s", String(", ").c_str()); launcherConsolePrintf("%s", String(trackball_axis_y).c_str());
        // launcherConsolePrintf("%s\n", String("]").c_str());
        if (trackball_axis_x < 0 || trackball_axis_y < 0) {
            ISR_rst();
            PrevPress = true;
        } // left , Up
        else if (trackball_axis_x > 0 || trackball_axis_y > 0) {
            ISR_rst();
            NextPress = true;
        } // right, Down
    }

    if (keyValue != (char)0x00) {
        if (!wakeUpScreen()) {
            AnyKeyPress = true;
        } else return;
        KeyStroke.Clear();
        KeyStroke.hid_keys.push_back(keyValue);
        if (keyValue == ' ') KeyStroke.exit_key = true; // key pressed to try to exit
        if (keyValue == (char)0x08) {
            KeyStroke.exit_key = true;
            KeyStroke.del = true;
        }

        if (keyValue == 'w') UpPress = true;
        if (keyValue == 's') DownPress = true;
        if (keyValue == 'a') PrevPress = true;
        if (keyValue == 'd') NextPress = true;

        if (keyValue == (char)0x0D) KeyStroke.enter = true;
        if (launcherGpioRead(SEL_BTN) == BTN_ACT) KeyStroke.fn = true;
        KeyStroke.word.push_back(keyValue);
        if (KeyStroke.del) EscPress = true;
        if (KeyStroke.enter) SelPress = true;
        KeyStroke.pressed = true;
        tm = launcherMillis();
    } else KeyStroke.pressed = false;

    if (launcherGpioRead(SEL_BTN) == BTN_ACT) {
        tm = launcherMillis();
        if (!wakeUpScreen()) {
            AnyKeyPress = true;
        } else return;
        SelPress = true;
    }

    if ((launcherMillis() - tm) > 190 || LongPress) { // one reading each 190ms
        if (touched) {

            // launcherConsolePrintf("\nPressed x=%d , y=%d, rot: %d", t.x, t.y, rotation);
            tm = launcherMillis();

            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
            touched = 0;
            return;
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    launcherGpioWrite(PIN_POWER_ON, LOW);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: _checkNextPagePress
** location: mykeyboard.cpp
** returns the key from the keyboard
**********************************************************************/
bool _checkNextPagePress() { return false; }

/*********************************************************************
** Function: _checkPrevPagePress
** location: mykeyboard.cpp
** returns the key from the keyboard
**********************************************************************/
bool _checkPrevPagePress() { return false; }
