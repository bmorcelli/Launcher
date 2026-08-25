#include "hal/bright/bright.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <globals.h>
#include <interface.h>
// Keyboard definitions
#define KB_I2C_ADDRESS 0x34
#define BQ25896_I2C_ADDRESS 0x6B
#define CAPS_LOCK 0x00
#define SHIFT 0x1c
#define KEY_LEFT_SHIFT 0x1c
#define KEY_FN 0x14
#define KEY_BACKSPACE 0x1d
#define KEY_ENTER 0x13
// Pinouts definitions
#define KEYBOARD_SDA 3
#define KEYBOARD_SCL 2
#define KEYBOARD_BL 46
#define ENCODER_INA 40
#define ENCODER_INB 41
#define ENCODER_KEY 7
#define HAS_BTN 1
#define SEL_BTN 7
#define BTN_ACT LOW
#define BK_BTN 0
#define EXPANDS_DRV_EN 0
#define EXPANDS_AMP_EN 1
#define EXPANDS_KB_RST 2
#define EXPANDS_LORA_EN 3
#define EXPANDS_GPS_EN 4
#define EXPANDS_NFC_EN 5
#define EXPANDS_GPS_RST 7
#define EXPANDS_KB_EN 8
#define EXPANDS_GPIO_EN 9
#define EXPANDS_SD_DET 10
#define EXPANDS_SD_PULLEN 11
#define EXPANDS_SD_EN 12

// GPIO expander
#include <ExtensionIOXL9555.hpp>
ExtensionIOXL9555 io;

// Rotary encoder
#include "hal/inputs/encoder.h"

static DeviceEncoder encoderCfg() {
    DeviceEncoder cfg;
    // A/B swapped on purpose: this board's physical wiring reports rotation
    // opposite to lilygo-t-embed-cc1101/m5stack-dinmeter for the Next/Prev
    // mapping hal_encoder_poll() assumes -- swapping the quadrature pins
    // flips the sign RotaryEncoder reports, correcting for it without
    // touching hal_encoder_poll()'s logic.
    cfg.pin_a = ENCODER_INB;
    cfg.pin_b = ENCODER_INA;
    cfg.pin_sel = SEL_BTN;
    cfg.pin_esc = BK_BTN;
    return cfg;
}

// Battery: charger + fuel gauge
#include "hal/device.h"
#include "hal/power/gauge.h"
#include "hal/power/pmic.h"
#include "idf/launcher_platform.h"
#define BATTERY_DESIGN_CAPACITY 1500

// Keyboard
#include <Adafruit_TCA8418.h>
Adafruit_TCA8418 keyboard;

bool fn_key_pressed = false;
bool shift_key_pressed = false;
bool caps_lock = false;

constexpr unsigned long TCA8418_REPEAT_START_MS = 350;
constexpr unsigned long TCA8418_REPEAT_MS = 150;

#define KB_ROWS 4
#define KB_COLS 10

struct KeyValue_t {
    const char value_first;
    const char value_second;
    const char value_third;
};

const KeyValue_t _key_value_map[KB_ROWS][KB_COLS] = {
    {{'q', 'Q', '1'},
     {'w', 'W', '2'},
     {'e', 'E', '3'},
     {'r', 'R', '4'},
     {'t', 'T', '5'},
     {'y', 'Y', '6'},
     {'u', 'U', '7'},
     {'i', 'I', '8'},
     {'o', 'O', '9'},
     {'p', 'P', '0'}},

    {{'a', 'A', '*'},
     {'s', 'S', '/'},
     {'d', 'D', '+'},
     {'f', 'F', '-'},
     {'g', 'G', '='},
     {'h', 'H', ':'},
     {'j', 'J', '\''},
     {'k', 'K', '"'},
     {'l', 'L', '@'},
     {KEY_ENTER, KEY_ENTER, KEY_ENTER}},

    {{KEY_FN, KEY_FN, KEY_FN},
     {'z', 'Z', '_'},
     {'x', 'X', '$'},
     {'c', 'C', ';'},
     {'v', 'V', '?'},
     {'b', 'B', '!'},
     {'n', 'N', ','},
     {'m', 'M', '.'},
     {SHIFT, SHIFT, CAPS_LOCK},
     {KEY_BACKSPACE, KEY_BACKSPACE, KEY_BACKSPACE}},

    {{' ', ' ', ' '}}
};

char getKeyChar(uint8_t k) {
    char keyVal;
    if (fn_key_pressed) {
        keyVal = _key_value_map[k / 10][k % 10].value_third;
    } else if (shift_key_pressed ^ caps_lock) {
        keyVal = _key_value_map[k / 10][k % 10].value_second;
    } else {
        keyVal = _key_value_map[k / 10][k % 10].value_first;
    }
    return keyVal;
}

int handleSpecialKeys(uint8_t k, bool pressed) {
    char keyVal = _key_value_map[k / 10][k % 10].value_first;
    switch (keyVal) {
        case KEY_FN: fn_key_pressed = !fn_key_pressed; return 1;
        case SHIFT: {
            shift_key_pressed = pressed;
            if (fn_key_pressed && shift_key_pressed) { caps_lock = !caps_lock; }
            return 1;
        }
        default: break;
    }
    return 0;
}

void _setup_gpio() {

    Wire.begin(KEYBOARD_SDA, KEYBOARD_SCL);

    launcherGpioInput(SEL_BTN);
    launcherGpioInput(BK_BTN);

    // before powering on, all CS signals should be pulled high and in an unselected state;
    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(SDCARD_CS);
    launcherGpioWrite(SDCARD_CS, HIGH);

    DevicePmic pmicCfg{SDA, SCL, BQ25896_I2C_ADDRESS};
    if (!hal_pmic_init(pmicCfg)) { launcherConsolePrintln("PMIC: Failed starting BQ25896"); }

    // Battery gauge
    DeviceGauge gaugeCfg{};
    gaugeCfg.design_capacity_mah = BATTERY_DESIGN_CAPACITY;
    hal_gauge_init(gaugeCfg);

    if (io.begin(Wire, 0x20)) {
        const uint8_t expands[] = {
            EXPANDS_KB_RST,
            EXPANDS_KB_EN,
            EXPANDS_SD_EN,
        };
        for (auto pin : expands) {
            io.pinMode(pin, OUTPUT);
            io.digitalWrite(pin, HIGH);
            delay(1);
        }
        io.pinMode(EXPANDS_SD_PULLEN, INPUT);
    } else {
        launcherConsolePrintf("%s\n", String("Initializing expander failed").c_str());
    }

    hal_encoder_init(encoderCfg(), EncoderLatchMode::FOUR3);

    uint8_t blPins[] = {TFT_BL, KEYBOARD_BL};
    hal_bright_attach(blPins, 2);

    // Initalise keyboard
    bool res = keyboard.begin(KB_I2C_ADDRESS, &Wire);
    if (!res) {
        launcherConsolePrintf("%s\n", String("Failed to find Keyboard").c_str());

    } else {
        launcherConsolePrintf("%s\n", String("Initializing Keyboard succeeded").c_str());
    }
    keyboard.matrix(KB_ROWS, KB_COLS);
    keyboard.flush();
}

int getBattery() { return hal_gauge_get_percent(); }

void _setBrightness(uint8_t brightval) {
    uint8_t blPins[] = {TFT_BL, KEYBOARD_BL};
    hal_bright_set(blPins, 2, brightval);
}

void InputHandler(void) {

    static unsigned long nextRepeatTime = 0;
    static unsigned long prevRepeatTime = 0;
    static unsigned long upRepeatTime = 0;
    static unsigned long downRepeatTime = 0;
    static bool nextHeld = false;
    static bool prevHeld = false;
    static bool upHeld = false;
    static bool downHeld = false;

    bool nextPulse = false;
    bool prevPulse = false;
    bool upPulse = false;
    bool downPulse = false;
    bool selPulse = false;
    bool escPulse = false;
    bool keyPulse = false;
    keyStroke pendingKey;

    while (keyboard.available() > 0) {
        int keyValue = keyboard.getEvent();
        bool pressed = keyValue & 0x80;
        keyValue &= 0x7F;
        keyValue--;

        if (keyValue / 10 >= KB_ROWS || keyValue % 10 >= KB_COLS) continue;
        if (handleSpecialKeys(keyValue, pressed) > 0) continue;

        uint8_t keyVal = getKeyChar(keyValue);
        if (pressed && !wakeUpScreen() && keyVal != '\0') {
            pendingKey.hid_keys.push_back(keyVal);
            if (keyVal == KEY_BACKSPACE) {
                pendingKey.del = true;
                pendingKey.exit_key = true;
                escPulse = true;
            }
            if (keyVal == KEY_ENTER) {
                pendingKey.enter = true;
                selPulse = true;
            }
            if (launcherGpioRead(SEL_BTN) == BTN_ACT) pendingKey.fn = true;
            if (keyVal == 'w') {
                upPulse = true;
                upRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
            }
            if (keyVal == 's') {
                downPulse = true;
                downRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
            }
            if (keyVal == 'a') {
                prevPulse = true;
                prevRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
            }
            if (keyVal == 'd') {
                nextPulse = true;
                nextRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
            }
            pendingKey.word.push_back(keyVal);
            pendingKey.pressed = true;
            keyPulse = true;
        }

        if (keyVal == 'w') upHeld = pressed;
        if (keyVal == 's') downHeld = pressed;
        if (keyVal == 'a') prevHeld = pressed;
        if (keyVal == 'd') nextHeld = pressed;
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

    hal_encoder_poll(encoderCfg());
}

void powerOff() { hal_pmic_shutdown(); }
