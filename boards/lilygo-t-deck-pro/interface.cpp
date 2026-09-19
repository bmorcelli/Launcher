#include "hal/bright/bright.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

static bool touch_OK = false;

// GPIO expander
#include <ExtensionIOXL9555.hpp>
ExtensionIOXL9555 io;

#include "hal/device.h"
#include "hal/power/gauge.h"
#include "hal/power/pmic.h"
#include "idf/launcher_platform.h"

#include <Adafruit_TCA8418.h>
#define BOARD_I2C_ADDR_KEYBOARD 0x34
#define KEYPAD_SDA 13
#define KEYPAD_SCL 14
#define KEYPAD_IRQ 15
#define KEYPAD_ROWS 4
#define KEYPAD_COLS 10

Adafruit_TCA8418 *keyboard;

constexpr unsigned long TCA8418_REPEAT_START_MS = 350;
constexpr unsigned long TCA8418_REPEAT_MS = 150;

#define BOARD_SDA 13
#define BOARD_SCL 14
#define TOUCH_INT 12
#define TOUCH_RST 45
#define TOUCH_RST2 38
#define BOARD_I2C_ADDR_TOUCH 0x1A

#define TDECKPRO_BQ25896_ADDRESS 0x6B

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

int variant = -1;
/*
variant = 0 -> 1.0
variant = 1 -> 1.1
variant = 2 -> max
variant = -1 -> unknown
*/

namespace {
bool hasI2cDevice(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
}

void detectBoardVariantAndPrepareDisplay(bool preDisplayInit) {
    if (variant == 2 || variant == 1) return;

    const bool hasDrv2605 = hasI2cDevice(0x5A);
    const bool hasXl9555 = hasI2cDevice(0x20);

    if (hasXl9555) {
        launcherConsolePrintln("T-Deck Pro MAX detected");
        variant = 2;

        if (preDisplayInit) {
            // The Max routes the display reset through the XL9555 expander.
            // Pulse it before the e-paper driver starts so GxEPD2 doesn't inherit
            // stale controller state from the previously booted firmware.
            if (io.begin(Wire, 0x20)) {
                io.pinMode(9, OUTPUT); // EXPANDS_KB_RST / display reset line
                io.digitalWrite(9, LOW);
                delay(20);
                io.digitalWrite(9, HIGH);
                delay(100);
            } else {
                launcherConsolePrintf("%s\n", String("Initializing expander failed").c_str());
            }
        }
        return;
    }

    if (preDisplayInit) return;

    if (hasDrv2605) {
        launcherConsolePrintln("T-Deck Pro 1.1 detected");
        variant = 1;
        pinMode(TOUCH_RST2, OUTPUT);
        digitalWrite(TOUCH_RST2, LOW);
        launcherDelayMs(10);
        digitalWrite(TOUCH_RST2, HIGH);
        delay(100);
    } else {
        // T-Deck Pro 1.0 has no distinct probe in this path, so once MAX and 1.1
        // are ruled out we treat the board as 1.0.
        launcherConsolePrintln("T-Deck Pro 1.0 detected");
        variant = 0;
        pinMode(TOUCH_RST, OUTPUT);
        digitalWrite(TOUCH_RST, LOW);
        launcherDelayMs(10);
        digitalWrite(TOUCH_RST, HIGH);
        delay(100);
    }
}
} // namespace

// Reset pin depends on the variant detected above: 1.0/1.1 have a real GPIO
// reset (TOUCH_RST/TOUCH_RST2, already pulsed by hand in
// detectBoardVariantAndPrepareDisplay() before hal_touch_init() runs, same
// as the driver's own reset via cfg.pin_rst); MAX has none (-1) -- its
// display/touch reset line is routed through the XL9555 expander instead.
static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    if (variant == 0) cfg.pin_rst = TOUCH_RST;
    else if (variant == 1) cfg.pin_rst = TOUCH_RST2;
    else cfg.pin_rst = -1;
    cfg.pin_irq = TOUCH_INT;
    return cfg;
}

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

    // Same bus the e-paper hangs off; the pins are the board's TFT_* flags.
    SPI.begin(TFT_SCLK, SDCARD_MISO, TFT_MOSI, TFT_CS);

    Wire.begin(BOARD_SDA, BOARD_SCL);
    launcherDelayMs(100);
    detectBoardVariantAndPrepareDisplay(true);

    // BQ25896 --- 0x6B
    Wire.beginTransmission(TDECKPRO_BQ25896_ADDRESS);
    if (Wire.endTransmission() == 0) {
        DevicePmic pmicCfg{BOARD_SDA, BOARD_SCL, TDECKPRO_BQ25896_ADDRESS};
        if (!hal_pmic_init(pmicCfg)) { launcherConsolePrintln("PMIC: Failed starting BQ25896"); }

        hal_gauge_init(DeviceGauge{});
    }
}

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
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);

    detectBoardVariantAndPrepareDisplay(false);
    if (variant == 2) {
        if (io.begin(Wire, 0x20)) {
            const uint8_t expands[] = {
                9, // EXPANDS_KB_RST / display reset line
                2, // GPS
            };
            for (auto pin : expands) {
                io.pinMode(pin, OUTPUT);
                io.digitalWrite(pin, HIGH);
                delay(1);
            }
        } else {
            launcherConsolePrintf("%s\n", String("Initializing expander failed").c_str());
        }
    }

    touch_OK = hal_touch_init(touchCfg(), BOARD_I2C_ADDR_TOUCH /* CST328_SLAVE_ADDRESS */);
    if (!touch_OK) {
        launcherConsolePrintf("%s\n", String("Failed to find Capacitive Touch !").c_str());
    } else {
        launcherConsolePrintf("%s\n", String("Find Capacitive Touch").c_str());
    }
}

int getBattery() { return hal_gauge_get_percent(); }

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

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

void InputHandler(void) {
    static long _tmptmp;
    static unsigned long nextRepeatTime = 0;
    static unsigned long prevRepeatTime = 0;
    static unsigned long upRepeatTime = 0;
    static unsigned long downRepeatTime = 0;
    static bool nextHeld = false;
    static bool prevHeld = false;
    static bool upHeld = false;
    static bool downHeld = false;

    LTouchPoint t;
    bool touched = touch_OK && hal_touch_read(touchCfg(), t);
    vTaskDelay(pdMS_TO_TICKS(5));
    if ((launcherMillis() - _tmptmp) > 200 || LongPress) { // one reading each 500ms
        if (launcherGpioRead(0) == LOW) NextPress = true;

        // launcherConsolePrintf("\nPressed x=%d , y=%d, rot: %d",t.x, t.y, rotation);
        if (touched) {

            // launcherConsolePrintf(
            //     "\nPressed x=%d , y=%d, rot: %d, millis=%d, tmp=%d",
            //     t.x,
            //     t.y,
            //     rotation,
            //     launcherMillis(),
            //     _tmptmp
            // );
            _tmptmp = launcherMillis();

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
            touched = 0;
        }
    }

    bool nextPulse = false;
    bool prevPulse = false;
    bool upPulse = false;
    bool downPulse = false;
    bool selPulse = false;
    bool escPulse = false;
    bool keyPulse = false;
    keyStroke pendingKey;

    vTaskDelay(pdMS_TO_TICKS(5));
    while (keyboard->available() > 0) {
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

        if (state == -1) continue;

        if (handleSpecialKeys(keyValue, state) > 0) continue;
        char keyVal = getKeyChar(keyValue);

        if (keyVal != '\0') {
            bool pressed = state == 1;
            if (keyVal == KEY_BACKSPACE) {
                if (pressed) {
                    pendingKey.pressed = true;
                    pendingKey.del = true;
                    pendingKey.exit_key = true;
                    escPulse = true;
                    keyPulse = true;
                }
            } else if (keyVal == KEY_ENTER) {
                if (pressed) {
                    pendingKey.enter = true;
                    pendingKey.pressed = true;
                    selPulse = true;
                    keyPulse = true;
                }
            } else if (keyVal == KEY_FN) {
                if (pressed) {
                    pendingKey.fn = true;
                    pendingKey.pressed = true;
                    keyPulse = true;
                }
            } else {
                if (keyVal == 'w') {
                    upHeld = pressed;
                    if (pressed) {
                        upPulse = true;
                        upRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
                    }
                }
                if (keyVal == 's') {
                    downHeld = pressed;
                    if (pressed) {
                        downPulse = true;
                        downRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
                    }
                }
                if (keyVal == 'a') {
                    prevHeld = pressed;
                    if (pressed) {
                        prevPulse = true;
                        prevRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
                    }
                }
                if (keyVal == 'd') {
                    nextHeld = pressed;
                    if (pressed) {
                        nextPulse = true;
                        nextRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
                    }
                }
                if (pressed) {
                    pendingKey.word.push_back(keyVal);
                    pendingKey.pressed = true;
                    keyPulse = true;
                }
            }
        }
    }

    unsigned long now = launcherMillis();
    if (nextHeld && now >= nextRepeatTime) {
        nextPulse = true;
        nextRepeatTime = now + TCA8418_REPEAT_MS;
    }
    if (prevHeld && now >= prevRepeatTime) {
        prevPulse = true;
        prevRepeatTime = now + TCA8418_REPEAT_MS;
    }
    if (upHeld && now >= upRepeatTime) {
        upPulse = true;
        upRepeatTime = now + TCA8418_REPEAT_MS;
    }
    if (downHeld && now >= downRepeatTime) {
        downPulse = true;
        downRepeatTime = now + TCA8418_REPEAT_MS;
    }

    if (keyPulse) KeyStroke = pendingKey;
    else if (!nextPulse && !prevPulse && !upPulse && !downPulse) KeyStroke.Clear();

    if (nextPulse || prevPulse || upPulse || downPulse || selPulse || escPulse || keyPulse) {
        AnyKeyPress = true;
        NextPress = nextPulse;
        PrevPress = prevPulse;
        UpPress = upPulse;
        DownPress = downPulse;
        SelPress = selPulse;
        EscPress = escPulse;
    }
}

void powerOff() {
    tft->fillScreen(BGCOLOR);
    initDisplay(true);
    tft->setTextSize(FG);
    tft->setTextColor(FGCOLOR);
    tft->drawCentreString("Powered OFF", tftWidth / 2, tftHeight - 100, 1);
    tft->display();
    launcherDelayMs(1000);
    hal_pmic_shutdown();
    while (1) launcherDelayMs(100);
}
