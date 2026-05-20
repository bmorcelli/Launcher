#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>
#include <interface.h>
TouchDrvCSTXXX touch;

#include <bq27220.h>
BQ27220 bq;

#define XPOWERS_CHIP_BQ25896
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
#define TOUCH_RST2 38
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

int variant = 0;
/*
variant = 0 -> 1.0
variant = 1 -> 1.1
variant = 2 -> max
*/

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    // LORA、SD、EPD use the same SPI, in order to avoid mutual influence;
    // before powering on, all CS signals should be pulled high and in an unselected state;
    launcherGpioOutput(BOARD_EPD_CS);
    launcherGpioWrite(BOARD_EPD_CS, HIGH);
    launcherGpioOutput(BOARD_SD_CS);
    launcherGpioWrite(BOARD_SD_CS, HIGH);
    launcherGpioOutput(BOARD_LORA_CS);
    launcherGpioWrite(BOARD_LORA_CS, HIGH);
    // Assuming that the previous touch was in sleep state, wake it up
    pinMode(TOUCH_INT, INPUT);

    launcherConsoleBegin(115200);

    // IO
    pinMode(0, INPUT_PULLUP);
    launcherGpioOutput(BOARD_KEYBOARD_LED);
    launcherGpioOutput(BOARD_MOTOR_PIN);
    launcherGpioOutput(BOARD_6609_EN); // enable 7682 module
    launcherGpioOutput(BOARD_LORA_EN); // enable LORA module
    launcherGpioOutput(BOARD_GPS_EN);  // enable GPS module
    launcherGpioOutput(BOARD_A7682E_PWRKEY);
    launcherGpioWrite(BOARD_KEYBOARD_LED, LOW);
    launcherGpioWrite(BOARD_MOTOR_PIN, LOW);
    launcherGpioWrite(BOARD_6609_EN, HIGH);
    launcherGpioWrite(BOARD_LORA_EN, HIGH);
    launcherGpioWrite(BOARD_GPS_EN, HIGH);
    launcherGpioWrite(BOARD_A7682E_PWRKEY, HIGH);

    SPI.begin(BOARD_SPI_SCK, SDCARD_MISO, BOARD_SPI_MOSI, BOARD_SPI_CS);

    Wire.begin(BOARD_SDA, BOARD_SCL);
    launcherDelayMs(100);

    // BQ25896 --- 0x6B
    Wire.beginTransmission(BQ25896_SLAVE_ADDRESS);
    if (Wire.endTransmission() == 0) {
        PPM.init(Wire, BOARD_SDA, BOARD_SCL, BQ25896_SLAVE_ADDRESS);
        PPM.setSysPowerDownVoltage(3300);
        PPM.setInputCurrentLimit(3250);
        launcherConsolePrintf("getInputCurrentLimit: %d mA\n", PPM.getInputCurrentLimit());
        PPM.disableCurrentLimitPin();
        PPM.setChargeTargetVoltage(4208);
        PPM.setPrechargeCurr(64);
        PPM.setChargerConstantCurr(832);
        PPM.getChargerConstantCurr();
        launcherConsolePrintf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
        PPM.enableMeasure();
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
    launcherConsolePrintf("%s\n", String("Scanning for I2C devices ...").c_str());
    for (address = 0x01; address < 0x7f; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
            launcherConsolePrintf("I2C device found at address 0x%02X\n", address);
            nDevices++;
        } else if (error != 2) {
            launcherConsolePrintf("Error %d at address 0x%02X\n", error, address);
        }
    }
    if (nDevices == 0) { launcherConsolePrintf("%s\n", String("No I2C devices found").c_str()); }
}
void _post_setup_gpio() {
    /*
     * The touch reset pin uses hardware pull-up,
     * and the function of setting the I2C device address cannot be used.
     * Use scanning to obtain the touch device address.*/

    // Scan I2C devices
    launcherConsolePrintf("%s\n", String("Scanning for I2C devices ...").c_str());
    scanDevices();

    keyboard = new Adafruit_TCA8418();
    if (!keyboard->begin(BOARD_I2C_ADDR_KEYBOARD, &Wire)) {
        launcherConsolePrintf("%s\n", String("keypad not found, check wiring & pullups!").c_str());
    }
    keyboard->matrix(KEYPAD_ROWS, KEYPAD_COLS);
    keyboard->flush();

    // Brightness control must be initialized after tft in this case @Pirata
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);

    Wire.beginTransmission(0x20); // test for XL9555, MAX exclusive IC
    if (Wire.endTransmission() == 0) {
        launcherConsolePrintln("T-Deck Pro MAX detected");
        variant = 2;
    }
    Wire.beginTransmission(0x5A); // test for DRV2605, t-deck Pro 1.1
    if (variant == 0 && Wire.endTransmission() == 0) {
        launcherConsolePrintln("T-Deck Pro 1.1 detected");
        variant = 1;
        pinMode(TOUCH_RST2, OUTPUT);
        digitalWrite(TOUCH_RST2, LOW);
        launcherDelayMs(10);
        digitalWrite(TOUCH_RST2, HIGH);
        delay(50);
    } else if (variant == 0) {
        launcherConsolePrintln("T-Deck Pro 1.0 detected");
        pinMode(TOUCH_RST, OUTPUT);
        digitalWrite(TOUCH_RST, LOW);
        launcherDelayMs(10);
        digitalWrite(TOUCH_RST, HIGH);
        delay(50);
    } else {
        launcherConsolePrintln("No version of T-Deck Pro detected");
        variant = -1;
    }

    uint8_t address = 0xFF;
    Wire.beginTransmission(CST328_SLAVE_ADDRESS);
    if (Wire.endTransmission() == 0) { address = CST328_SLAVE_ADDRESS; }

    uint8_t touchAddress = 0;
    if (variant == 0) touch.setPins(TOUCH_RST, TOUCH_INT);
    else if (variant == 1) touch.setPins(TOUCH_RST2, TOUCH_INT);
    else touch.setPins(-1, TOUCH_INT);
    bool hasTouch = true;
    hasTouch = touch.begin(Wire, address, BOARD_SDA, BOARD_SCL);
    if (!hasTouch) {
        launcherConsolePrintf("%s\n", String("Failed to find Capacitive Touch !").c_str());
    } else {
        launcherConsolePrintf("%s\n", String("Find Capacitive Touch").c_str());
    }
    launcherConsolePrintf("%s", String("Model :").c_str());
    launcherConsolePrintf("%s\n", String(touch.getModelName()).c_str());
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
        launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

struct LTouchPointPro {
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
    launcherConsolePrintf(
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
    LTouchPointPro t;
    uint8_t touched = 0;
    touched = touch.getPoint(&t.x, &t.y, 1);
    if ((launcherMillis() - _tmptmp) > 250 || LongPress) { // one reading each 500ms
        if (launcherGpioRead(0) == LOW) NextPress = true;

        // launcherConsolePrintf("\nPressed x=%d , y=%d, rot: %d",t.x, t.y, rotation);
        if (touched) {
            touch.reset();

            launcherConsolePrintf(
                "\nPressed x=%d , y=%d, rot: %d, millis=%d, tmp=%d",
                t.x,
                t.y,
                rotation,
                launcherMillis(),
                _tmptmp
            );
            _tmptmp = launcherMillis();

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
                KeyStroke.pressed = true;
                KeyStroke.del = true;
                EscPress = true;
                KeyStroke.exit_key = true;
            } else if (keyVal == KEY_ENTER) {
                KeyStroke.enter = true;
                SelPress = true;
            } else if (keyVal == KEY_FN) {
                KeyStroke.fn = true;
            } else {
                KeyStroke.word.push_back(keyVal);
                if (keyVal == 'w') UpPress = true;
                if (keyVal == 's') DownPress = true;
                if (keyVal == 'a') PrevPress = true;
                if (keyVal == 'd') NextPress = true;
                KeyStroke.pressed = true;
            }
        }
        _tmptmp = launcherMillis();
    } else KeyStroke.Clear();
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    tft->fillScreen(BGCOLOR);
    initDisplay(true);
    launcherDelayMs(1000);
    PPM.shutdown();
    while (1) launcherDelayMs(100);
}
