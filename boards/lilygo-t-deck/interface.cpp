#include "TouchDrvGT911.hpp"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>
TouchDrvGT911 touch;

struct TouchPointPro {
    int16_t x = 0;
    int16_t y = 0;
};

// Setup for Trackball
void IRAM_ATTR ISR_up();
void IRAM_ATTR ISR_down();
void IRAM_ATTR ISR_left();
void IRAM_ATTR ISR_right();

volatile bool trackball_interrupted = false;
volatile int8_t trackball_up_count = 0;
volatile int8_t trackball_down_count = 0;
volatile int8_t trackball_left_count = 0;
volatile int8_t trackball_right_count = 0;

#define TRACKBALL_THRESHOLD 2

void IRAM_ATTR ISR_up() {
    if (++trackball_up_count >= TRACKBALL_THRESHOLD) trackball_interrupted = true;
}
void IRAM_ATTR ISR_down() {
    if (++trackball_down_count >= TRACKBALL_THRESHOLD) trackball_interrupted = true;
}
void IRAM_ATTR ISR_left() {
    if (++trackball_left_count >= TRACKBALL_THRESHOLD) trackball_interrupted = true;
}
void IRAM_ATTR ISR_right() {
    if (++trackball_right_count >= TRACKBALL_THRESHOLD) trackball_interrupted = true;
}

void ISR_rst() {
    trackball_up_count = 0;
    trackball_down_count = 0;
    trackball_left_count = 0;
    trackball_right_count = 0;
    trackball_interrupted = false;
}

#define LILYGO_KB_SLAVE_ADDRESS 0x55
#define LILYGO_KB_BRIGHTNESS_CMD 0x01
#define KB_I2C_SDA 18
#define KB_I2C_SCL 8
#define SEL_BTN 0
#define UP_BTN 3
#define DW_BTN 15
#define L_BTN 2
#define R_BTN 1
#define PIN_POWER_ON 10
#define BOARD_TOUCH_INT 16
/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    delay(500); // time to ESP32C3 start and enable the keyboard
    if (!Wire.begin(KB_I2C_SDA, KB_I2C_SCL)) Serial.println("Fail starting ESP32-C3 keyboard");

    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    pinMode(SEL_BTN, INPUT);
    pinMode(BOARD_TOUCH_INT, INPUT);
    touch.setPins(-1, BOARD_TOUCH_INT);
    if (!touch.begin(Wire, GT911_SLAVE_ADDRESS_L)) {
        Serial.println("Failed to find GT911 - check your wiring!");
    }

    pinMode(9, OUTPUT); // LoRa Radio CS Pin to HIGH (Inhibit the SPI Communication for this module)
    digitalWrite(9, HIGH);

    // Setup for Trackball
    pinMode(UP_BTN, INPUT_PULLUP);
    attachInterrupt(UP_BTN, ISR_up, FALLING);
    pinMode(DW_BTN, INPUT_PULLUP);
    attachInterrupt(DW_BTN, ISR_down, FALLING);
    pinMode(L_BTN, INPUT_PULLUP);
    attachInterrupt(L_BTN, ISR_left, FALLING);
    pinMode(R_BTN, INPUT_PULLUP);
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
    static unsigned long tm = millis();
    TouchPointPro t;
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
    touched = touch.getPoint(&t.x, &t.y);
    delay(1);
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    while (Wire.available() > 0) {
        keyValue = Wire.read();
        delay(1);
    }
    if (millis() - tm < 200 && !LongPress) return;

    // 0 - UP
    // 1 - Down
    // 2 - Left
    // 3 - Right
    if (trackball_interrupted) {
        uint8_t xx = 1;
        uint8_t yy = 1;
        xx += trackball_left_count ? 1 : 0;
        xx -= trackball_right_count ? 1 : 0;
        yy -= trackball_up_count ? 1 : 0;
        yy += trackball_down_count ? 1 : 0;
        if (xx == 1 && yy == 1) {
            ISR_rst();
        } else {
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;
        }
        // Print "bot - xx - yy",  1 is normal value for xx and yy 0 and 2 means movement on the axis
        // Serial.print(bot); Serial.print("-"); Serial.print(xx); Serial.print("-"); Serial.println(yy);
        if (xx < 1 || yy < 1) {
            ISR_rst();
            PrevPress = true;
        } // left , Up
        else if (xx > 1 || yy > 1) {
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
        if (keyValue == (char)0x0D) KeyStroke.enter = true;
        if (digitalRead(SEL_BTN) == BTN_ACT) KeyStroke.fn = true;
        KeyStroke.word.push_back(keyValue);
        if (KeyStroke.del) EscPress = true;
        if (KeyStroke.enter) SelPress = true;
        KeyStroke.pressed = true;
        tm = millis();
    } else KeyStroke.pressed = false;

    if (digitalRead(SEL_BTN) == BTN_ACT) {
        tm = millis();
        if (!wakeUpScreen()) {
            AnyKeyPress = true;
        } else return;
        SelPress = true;
    }

    if ((millis() - tm) > 190 || LongPress) { // one reading each 190ms
        if (touched) {

            // Serial.printf("\nPressed x=%d , y=%d, rot: %d", t.x, t.y, rotation);
            tm = millis();

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
void powerOff() {}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {}

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
