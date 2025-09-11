#include "powerSave.h"
#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>
#include <interface.h>
TouchDrvCSTXXX touch;

#include <bq27220.h>
BQ27220 bq;

#include <XPowersLib.h>
XPowersPPM PPM;

#include <Adafruit_TCA8418.h>
#define BOARD_I2C_ADDR_KEYBOARD 0x34
#define KEYPAD_SDA 13
#define KEYPAD_SCL 14
#define KEYPAD_IRQ 15
#define KEYPAD_ROWS 4
#define KEYPAD_COLS 10

Adafruit_TCA8418 *keyboard;

#define BOARD_SDA 13
#define BOARD_SCL 14
#define TOUCH_INT 12
#define TOUCH_RST 45
#define BOARD_I2C_ADDR_TOUCH 0x1A

#define BOARD_EPD_CS 34
#define BOARD_LORA_CS 3
#define BOARD_SD_CS 48
#define BOARD_GPS_EN 39  // enable GPS module
#define BOARD_1V8_EN 38  // enable gyroscope module
#define BOARD_6609_EN 41 // enable 7682 module
#define BOARD_LORA_EN 46 // enable LORA module
#define BOARD_MOTOR_PIN 2
#define BOARD_KEYBOARD_LED 42
#define BOARD_A7682E_PWRKEY 40

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    // LORA、SD、EPD use the same SPI, in order to avoid mutual influence;
    // before powering on, all CS signals should be pulled high and in an unselected state;
    pinMode(BOARD_EPD_CS, OUTPUT);
    digitalWrite(BOARD_EPD_CS, HIGH);
    pinMode(BOARD_SD_CS, OUTPUT);
    digitalWrite(BOARD_SD_CS, HIGH);
    pinMode(BOARD_LORA_CS, OUTPUT);
    digitalWrite(BOARD_LORA_CS, HIGH);
    // Assuming that the previous touch was in sleep state, wake it up
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, HIGH);
    delay(1);
    digitalWrite(TOUCH_INT, LOW);

    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(1);
    digitalWrite(TOUCH_RST, HIGH);

    Serial.begin(115200);

    // IO
    pinMode(0, INPUT);
    pinMode(BOARD_KEYBOARD_LED, OUTPUT);
    pinMode(BOARD_MOTOR_PIN, OUTPUT);
    pinMode(BOARD_6609_EN, OUTPUT); // enable 7682 module
    pinMode(BOARD_LORA_EN, OUTPUT); // enable LORA module
    pinMode(BOARD_GPS_EN, OUTPUT);  // enable GPS module
    pinMode(BOARD_1V8_EN, OUTPUT);  // enable gyroscope module
    pinMode(BOARD_A7682E_PWRKEY, OUTPUT);
    digitalWrite(BOARD_KEYBOARD_LED, LOW);
    digitalWrite(BOARD_MOTOR_PIN, LOW);
    digitalWrite(BOARD_6609_EN, HIGH);
    digitalWrite(BOARD_LORA_EN, HIGH);
    digitalWrite(BOARD_GPS_EN, HIGH);
    digitalWrite(BOARD_1V8_EN, HIGH);
    digitalWrite(BOARD_A7682E_PWRKEY, HIGH);

    SPI.begin(BOARD_SPI_SCK, SDCARD_MISO, BOARD_SPI_MOSI, BOARD_SPI_CS);

    Wire.begin(BOARD_SDA, BOARD_SCL);
    delay(500);

    // BQ25896 --- 0x6B
    Wire.beginTransmission(BQ25896_SLAVE_ADDRESS);
    if (Wire.endTransmission() == 0) {
        // battery_25896.begin();
        PPM.init(Wire, BOARD_SDA, BOARD_SCL, BQ25896_SLAVE_ADDRESS);
        // Set the minimum operating voltage. Below this voltage, the PPM will protect
        PPM.setSysPowerDownVoltage(3300);
        // Set input current limit, default is 500mA
        PPM.setInputCurrentLimit(3250);
        Serial.printf("getInputCurrentLimit: %d mA\n", PPM.getInputCurrentLimit());
        // Disable current limit pin
        PPM.disableCurrentLimitPin();
        // Set the charging target voltage, Range:3840 ~ 4608mV ,step:16 mV
        PPM.setChargeTargetVoltage(4208);
        // Set the precharge current , Range: 64mA ~ 1024mA ,step:64mA
        PPM.setPrechargeCurr(64);
        // The premise is that Limit Pin is disabled, or it will only follow the maximum charging current set
        // by Limi tPin. Set the charging current , Range:0~5056mA ,step:64mA
        PPM.setChargerConstantCurr(832);
        // Get the set charging current
        PPM.getChargerConstantCurr();
        Serial.printf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
        PPM.enableADCMeasure();
        PPM.enableCharge();
        PPM.disableOTG();
    }
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_Bits 8
#define TFT_BRIGHT_FREQ 5000
#define TFT_BL 40

void scanDevices(void) {
    byte error, address;
    int nDevices = 0;
    Serial.println("Scanning for I2C devices ...");
    for (address = 0x01; address < 0x7f; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
            Serial.printf("I2C device found at address 0x%02X\n", address);
            nDevices++;
        } else if (error != 2) {
            Serial.printf("Error %d at address 0x%02X\n", error, address);
        }
    }
    if (nDevices == 0) { Serial.println("No I2C devices found"); }
}
void _post_setup_gpio() {
    /*
     * The touch reset pin uses hardware pull-up,
     * and the function of setting the I2C device address cannot be used.
     * Use scanning to obtain the touch device address.*/

    // Scan I2C devices
    Serial.println("Scanning for I2C devices ...");
    scanDevices();
    uint8_t address = 0xFF;
    Wire.beginTransmission(CST328_SLAVE_ADDRESS);
    if (Wire.endTransmission() == 0) { address = CST328_SLAVE_ADDRESS; }

    uint8_t touchAddress = 0;
    touch.setPins(TOUCH_RST, TOUCH_INT);
    bool hasTouch = true;
    hasTouch = touch.begin(Wire, address, BOARD_SDA, BOARD_SCL);
    if (!hasTouch) {
        Serial.println("Failed to find Capacitive Touch !");
    } else {
        Serial.println("Find Capacitive Touch");
    }
    Serial.print("Model :");
    Serial.println(touch.getModelName());

    keyboard = new Adafruit_TCA8418();
    if (!keyboard->begin(BOARD_I2C_ADDR_KEYBOARD, &Wire)) {
        Serial.println("keypad not found, check wiring & pullups!");
    }
    keyboard->matrix(KEYPAD_ROWS, KEYPAD_COLS);
    // flush the internal buffer
    keyboard->flush();
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int percent = 0;
    percent = bq.getChargePcnt();

    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
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
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 250) / 100);

    log_i("dutyCycle for bright 0-255: %d", dutyCycle);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        Serial.println("Failed to set brightness");
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

struct TouchPointPro {
    int16_t x = 0;
    int16_t y = 0;
};

#define KB_ROWS 4
#define KB_COLS 10
#define KEYPAD_PRESS_VAL_MIN 129
#define KEYPAD_PRESS_VAL_MAX 163
#define KEYPAD_RELEASE_VAL_MIN 1
#define KEYPAD_RELEASE_VAL_MAX 35

#define SHIFT 0x80
#define KEY_LEFT_CTRL 0x80
#define KEY_LEFT_SHIFT 0x81
#define KEY_LEFT_ALT 0x82
#define KEY_OPT 0x00
#define KEY_FN 0xff
#define KEY_BACKSPACE 0x2a
#define KEY_ENTER 0x28

struct KeyValue_t {
    const char value_first;
    const char value_second;
    const char value_third;
};
bool fn_key_pressed = false;
bool shift_key_pressed = false;
bool caps_lock = false;
const KeyValue_t _key_value_map[KB_ROWS][KB_COLS] = {
    {{'q', 'Q', '#'},
     {'w', 'W', '1'},
     {'e', 'E', '2'},
     {'r', 'R', '3'},
     {'t', 'T', '('},
     {'y', 'Y', ')'},
     {'u', 'U', '_'},
     {'i', 'I', '-'},
     {'o', 'O', '+'},
     {'p', 'P', '@'}                                 },

    {{'a', 'A', '*'},
     {'s', 'S', '4'},
     {'d', 'D', '5'},
     {'f', 'F', '6'},
     {'g', 'G', '/'},
     {'h', 'H', ':'},
     {'j', 'J', ';'},
     {'k', 'K', '\''},
     {'l', 'L', '"'},
     {KEY_BACKSPACE, KEY_BACKSPACE, KEY_BACKSPACE}   },

    {{KEY_LEFT_ALT, KEY_LEFT_ALT, KEY_LEFT_ALT},
     {'z', 'Z', '7'},
     {'x', 'X', '8'},
     {'c', 'C', '9'},
     {'v', 'V', '?'},
     {'b', 'B', '!'},
     {'n', 'N', ','},
     {'m', 'M', '.'},
     {'$', '0' /*Sound*/, '0' /*Sound*/},
     {KEY_ENTER, KEY_ENTER, KEY_ENTER}               },

    {{' ', ' ', ' '},
     {' ', ' ', ' '},
     {' ', ' ', ' '},
     {' ', ' ', ' '},
     {' ', ' ', ' '},
     {KEY_LEFT_SHIFT, KEY_LEFT_SHIFT, KEY_LEFT_SHIFT},
     {KEY_OPT, KEY_OPT, '0'},
     {' ', ' ', ' '},
     {KEY_FN, KEY_FN, KEY_FN},
     {KEY_LEFT_SHIFT, KEY_LEFT_SHIFT, KEY_LEFT_SHIFT}}
};

char getKeyChar(uint8_t k) {
    char keyVal;
    if (fn_key_pressed) {
        keyVal = _key_value_map[k / 10][(KEYPAD_COLS - 1) - k % 10].value_third;
    } else if (shift_key_pressed ^ caps_lock) {
        keyVal = _key_value_map[k / 10][(KEYPAD_COLS - 1) - k % 10].value_second;
    } else {
        keyVal = _key_value_map[k / 10][(KEYPAD_COLS - 1) - k % 10].value_first;
    }
    Serial.printf(
        "Key pressed: %c (hex: 0x%02X, k=%d, fn=%d, shift=%d, caps=%d)\n",
        keyVal,
        (int)keyVal,
        k,
        fn_key_pressed,
        shift_key_pressed,
        caps_lock
    );
    return keyVal;
}

int handleSpecialKeys(uint8_t k, bool pressed) {
    char keyVal = _key_value_map[k / 10][(KEYPAD_COLS - 1) - k % 10].value_first;
    switch (keyVal) {
        case KEY_FN: fn_key_pressed = !fn_key_pressed; return 1;
        case KEY_LEFT_SHIFT: {
            shift_key_pressed = pressed;
            if (fn_key_pressed && shift_key_pressed) { caps_lock = !caps_lock; }
            return 1;
        }
        default: break;
    }
    return 0;
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long _tmptmp;
    TouchPointPro t;
    uint8_t touched = 0;
    touched = touch.getPoint(&t.x, &t.y);
    if ((millis() - _tmptmp) > 150 || LongPress) { // one reading each 500ms
        if (digitalRead(0) == LOW) NextPress = true;

        // Serial.printf("\nPressed x=%d , y=%d, rot: %d",t.x, t.y, rotation);
        if (touched) {

            Serial.printf(
                "\nPressed x=%d , y=%d, rot: %d, millis=%d, tmp=%d", t.x, t.y, rotation, millis(), _tmptmp
            );
            _tmptmp = millis();

            // if(!wakeUpScreen()) AnyKeyPress = true;
            // else goto END;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
            touched = 0;
        }
    END:
        yield();
    }

    uint8_t keyValue = 0;
    char keyVal = '\0';
    if (keyboard->available() > 0) {
        int keyValue = keyboard->getEvent();
        int state = -1;
        if (keyValue >= KEYPAD_RELEASE_VAL_MIN && keyValue <= KEYPAD_RELEASE_VAL_MAX) { // release event
            keyValue = keyValue - KEYPAD_RELEASE_VAL_MIN;
            state = 0;
        }
        if (keyValue >= KEYPAD_PRESS_VAL_MIN && keyValue <= KEYPAD_PRESS_VAL_MAX) { // press event
            keyValue = keyValue - KEYPAD_PRESS_VAL_MIN;
            state = 1; // pressed
        }

        if (state == -1) return;

        if (handleSpecialKeys(keyValue, state) > 0) return;
        keyVal = getKeyChar(keyValue);

        if (keyVal != '\0') {
            KeyStroke.Clear();
            if (keyVal == KEY_BACKSPACE) {
                KeyStroke.del = true;
                EscPress = true;
            } else if (keyVal == KEY_ENTER) {
                KeyStroke.enter = true;
                SelPress = true;
            } else if (keyVal == KEY_FN) {
                KeyStroke.fn = true;
            } else {
                KeyStroke.word.push_back(keyVal);
                KeyStroke.pressed = true;
            }
            _tmptmp = millis();
        }
    } else KeyStroke.Clear();
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
