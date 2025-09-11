#include "powerSave.h"
#include <Adafruit_TCA8418.h>
#include <Keyboard.h>
#include <Wire.h>
#include <interface.h>

// Cardputer and 1.1 keyboard
Keyboard_Class Keyboard;
// TCA8418 keyboard controller for ADV variant
Adafruit_TCA8418 tca;
bool UseTCA8418 = false; // Set to true to use TCA8418 (Cardputer ADV)

// Keyboard state variables
bool fn_key_pressed = false;
bool shift_key_pressed = false;
bool caps_lock = false;

// Key value mapping for 4x14 keyboard
struct ADVKeyValue_t {
    const char value_first;
    const char value_second;
    const char value_third;
};

const ADVKeyValue_t _adv_key_value_map[4][14] = {
    {{'`', '~', '`'},
     {'1', '!', '1'},
     {'2', '@', '2'},
     {'3', '#', '3'},
     {'4', '$', '4'},
     {'5', '%', '5'},
     {'6', '^', '6'},
     {'7', '&', '7'},
     {'8', '*', '8'},
     {'9', '(', '9'},
     {'0', ')', '0'},
     {'-', '_', '-'},
     {'=', '+', '='},
     {'\b', '\b', '\b'}}, // Backspace

    {{'\t', '\t', '\t'}, // Tab
     {'q', 'Q', 'q'},
     {'w', 'W', 'w'},
     {'e', 'E', 'e'},
     {'r', 'R', 'r'},
     {'t', 'T', 't'},
     {'y', 'Y', 'y'},
     {'u', 'U', 'u'},
     {'i', 'I', 'i'},
     {'o', 'O', 'o'},
     {'p', 'P', 'p'},
     {'[', '{', '['},
     {']', '}', ']'},
     {'\\', '|', '\\'} },

    {{0xFF, 0xFF, 0xFF}, // FN key (special)
     {0x81, 0x81, 0x81}, // Shift key (special)
     {'a', 'A', 'a'},
     {'s', 'S', 's'},
     {'d', 'D', 'd'},
     {'f', 'F', 'f'},
     {'g', 'G', 'g'},
     {'h', 'H', 'h'},
     {'j', 'J', 'j'},
     {'k', 'K', 'k'},
     {'l', 'L', 'l'},
     {';', ':', ';'},
     {'\'', '\"', '\''},
     {'\r', '\r', '\r'}}, // Enter

    {{0x80, 0x80, 0x80}, // Ctrl key (special)
     {0x83, 0x83, 0x83}, // OPT key (special)
     {0x82, 0x82, 0x82}, // Alt key (special)
     {'z', 'Z', 'z'},
     {'x', 'X', 'x'},
     {'c', 'C', 'c'},
     {'v', 'V', 'v'},
     {'b', 'B', 'b'},
     {'n', 'N', 'n'},
     {'m', 'M', 'm'},
     {',', '<', ','},
     {'.', '>', '.'},
     {'/', '?', '/'},
     {' ', ' ', ' '}   }
};

int handleSpecialKeys(uint8_t row, uint8_t col, bool pressed);
void mapRawKeyToPhysical(uint8_t rawValue, uint8_t &row, uint8_t &col);

char getKeyChar(uint8_t row, uint8_t col) {
    char keyVal;
    if (fn_key_pressed) {
        keyVal = _adv_key_value_map[row][col].value_third;
    } else if (shift_key_pressed ^ caps_lock) {
        keyVal = _adv_key_value_map[row][col].value_second;
    } else {
        keyVal = _adv_key_value_map[row][col].value_first;
    }
    return keyVal;
}

int handleSpecialKeys(uint8_t row, uint8_t col, bool pressed) {
    char keyVal = _key_value_map[row][col].value_first;
    switch (keyVal) {
        case 0xFF:
            if (pressed) fn_key_pressed = !fn_key_pressed;
            return 1;
        case 0x81:
            shift_key_pressed = pressed;
            if (pressed && fn_key_pressed) caps_lock = !caps_lock;
            return 1;
        default: break;
    }
    return 0;
}

/***************************************************************************************
** Function name: mapRawKeyToPhysical()
** Location: interface.cpp
** Description:   initial mapping for keyboard
***************************************************************************************/
inline void mapRawKeyToPhysical(uint8_t keyvalue, uint8_t &row, uint8_t &col) {
    const uint8_t u = keyvalue % 10; // 1..8
    const uint8_t t = keyvalue / 10; // 0..6

    if (u >= 1 && u <= 8 && t <= 6) {
        const uint8_t u0 = u - 1;   // 0..7
        row = u0 & 0x03;            // bits [1:0] => 0..3
        col = (t << 1) | (u0 >> 2); // t*2 + bit2(u0) => 0..13
    } else {
        row = 0xFF; // invalid
        col = 0xFF;
    }
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    //    Keyboard.begin();
    pinMode(0, INPUT);
    pinMode(10, INPUT); // Pin that reads the Battery voltage
    pinMode(5, OUTPUT);
    // Set GPIO5 HIGH for SD card compatibility (thx for the tip @bmorcelli & 7h30th3r0n3)
    digitalWrite(5, HIGH);
}

void _post_setup_gpio() {
    // Initialize TCA8418 I2C keyboard controller
    Serial.println("DEBUG: Cardputer ADV - Initializing TCA8418 keyboard");

    // Use correct I2C pins for Cardputer ADV
    Serial.printf("DEBUG: Initializing I2C with SDA=%d, SCL=%d\n", TCA8418_SDA_PIN, TCA8418_SCL_PIN);
    Wire.begin(TCA8418_SDA_PIN, TCA8418_SCL_PIN);
    delay(100);

    // Scan I2C bus to see what's available
    Serial.println("DEBUG: Scanning I2C bus...");
    byte found_devices = 0;
    for (byte i = 1; i < 127; i++) {
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0) {
            Serial.printf("DEBUG: Found I2C device at address 0x%02X\n", i);
            found_devices++;
        }
    }
    Serial.printf("DEBUG: Found %d I2C devices\n", found_devices);

    // Try to initialize TCA8418
    Serial.printf("DEBUG: Attempting to initialize TCA8418 at address 0x%02X\n", TCA8418_I2C_ADDR);
    UseTCA8418 = tca.begin(TCA8418_I2C_ADDR, &Wire);

    if (!UseTCA8418) {
        Serial.println("ADV  : Failed to initialize TCA8418!");
        Serial.println("Probable standard Cardputer detected, switching to Keyboard library");
        Wire.end();
        Keyboard.begin();
        return;
    }

    Serial.println("DEBUG: TCA8418 found and initialized successfully!");

    // Configure the matrix (7 rows x 8 columns)
    Serial.println("DEBUG: Configuring TCA8418 matrix (7x8)");
    // Reset the device to ensure clean state
    tca.writeRegister(TCA8418_REG_CFG, 0x00);
    delay(10);

    // Configure for 4 rows and 14 columns
    // Rows 0-3 as outputs, columns  4-17 as inputs
    tca.writeRegister(TCA8418_REG_GPIO_DIR_1, 0x0F); // GPIO0-3: outputs, GPIO4-7: inputs
    tca.writeRegister(TCA8418_REG_GPIO_DIR_2, 0xFF); // GPIO8-15: inputs
    tca.writeRegister(TCA8418_REG_GPIO_DIR_3, 0x03); // GPIO16-17: inputs

    // Set all used pins as keypad
    tca.writeRegister(TCA8418_REG_KP_GPIO_1, 0xFF); // GPIO0-7 as keypad
    tca.writeRegister(TCA8418_REG_KP_GPIO_2, 0xFF); // GPIO8-15 as keypad
    tca.writeRegister(TCA8418_REG_KP_GPIO_3, 0x03); // GPIO16-17 as keypad

    // Enable pull-ups on all inputs
    tca.writeRegister(TCA8418_REG_GPIO_PULL_1, 0xF0); // Pull-ups on GPIO4-7
    tca.writeRegister(TCA8418_REG_GPIO_PULL_2, 0xFF); // Pull-ups on GPIO8-15
    tca.writeRegister(TCA8418_REG_GPIO_PULL_3, 0x03); // Pull-ups on GPIO16-17

    // Configure interrupts
    tca.writeRegister(
        TCA8418_REG_CFG,
        TCA8418_REG_CFG_KE_IEN | // Enable key event interrupt
            TCA8418_REG_CFG_AI   // Auto-increment
    );

    // Clear interrupts
    tca.writeRegister(TCA8418_REG_INT_STAT, 0xFF);
    Serial.println("DEBUG: TCA8418 configured for polling mode (interrupts disabled)");
}
/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    uint8_t percent;
    uint32_t volt = analogReadMilliVolts(GPIO_NUM_10);

    float mv = volt;
    percent = (mv - 3300) * 100 / (float)(4150 - 3350);

    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
    } else {
        int bl = MINBRIGHT + round(((255 - MINBRIGHT) * brightval / 100));
        analogWrite(TFT_BL, bl);
    }
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    static unsigned long lastCheck = 0;
    static unsigned long lastKeyTime = 0;
    static uint8_t lastKeyValue = 0;

    if (millis() - tm < 200 && !LongPress) return;

    if (digitalRead(0) == LOW) { // GPIO0 button, shoulder button
        tm = millis();
        AnyKeyPress = true;
        if (!wakeUpScreen()) yield();
        else return;
        SelPress = true;
        AnyKeyPress = true;
    }
    if (UseTCA8418) {
        if (millis() - lastCheck < 100) return; // Poll TCA8418 every 100ms for key events
        lastCheck = millis();
        if (tca.available() <= 0) return;
        int keyEvent = tca.getEvent();
        bool pressed = !(keyEvent & 0x80); // Bit 7: 0=pressed, 1=released
        uint8_t value = keyEvent & 0x7F;   // Bits 0-6: key value

        // // Debounce check
        // if (millis() - lastKeyTime < 50 && value == lastKeyValue) { return; }
        // lastKeyTime = millis();
        // lastKeyValue = value;

        // Map raw value to physical position
        uint8_t row, col;
        mapRawKeyToPhysical(value, row, col);

        Serial.printf("Key event: raw=%d, pressed=%d, row=%d, col=%d\n", value, pressed, row, col);

        if (row >= 4 || col >= 14) return;

        if (wakeUpScreen()) return;

        AnyKeyPress = true;

        if (handleSpecialKeys(row, col, pressed) > 0) return;

        if (!pressed) {
            KeyStroke.Clear();
            LongPressTmp = false;
            return;
        }

        keyStroke key;
        char keyVal = getKeyChar(row, col);

        Serial.printf("Key pressed: %c (0x%02X) at row=%d, col=%d\n", keyVal, keyVal, row, col);

        if (keyVal == 0x08) {
            key.del = true;
            key.word.emplace_back(KEY_BACKSPACE);
            EscPress = true;
        } else if (keyVal == 0x60) {
            EscPress = true;
        } else if (keyVal == 0x0D) {
            key.enter = true;
            key.word.emplace_back(KEY_ENTER);
            SelPress = true;
        } else if (keyVal == 0x2C || keyVal == 0x3B) {
            PrevPress = true;
            key.word.emplace_back(keyVal);
        } else if (keyVal == 0x2F || keyVal == 0x2E) {
            NextPress = true;
            key.word.emplace_back(keyVal);
        } else if (keyVal == 0x09) {
            key.word.emplace_back(KEY_TAB);
        } else if (keyVal == 0xFF) {
            key.fn = true;
        } else if (keyVal == 0x81) {
            key.modifier_keys.emplace_back(KEY_LEFT_SHIFT);
        } else if (keyVal == 0x80) {
            key.modifier_keys.emplace_back(KEY_LEFT_CTRL);
        } else if (keyVal == 0x82) {
            key.modifier_keys.emplace_back(KEY_LEFT_ALT);
        } else {
            key.word.emplace_back(keyVal);
        }
        key.pressed = true;
        KeyStroke = key;
        tm = millis();
    } else {
        Keyboard.update();
        if (!Keyboard.isPressed()) {
            KeyStroke.Clear();
            LongPressTmp = false;
            return;
        }
        tm = millis();
        if (!wakeUpScreen()) yield();
        else return;

        keyStroke key;
        Keyboard_Class::KeysState status = Keyboard.keysState();
        for (auto i : status.hid_keys) key.hid_keys.push_back(i);
        for (auto i : status.word) {
            key.word.push_back(i);
            if (i == '`') key.exit_key = true; // key pressed to try to exit
        }
        for (auto i : status.modifier_keys) key.modifier_keys.push_back(i);
        if (status.del) key.del = true;
        if (status.enter) key.enter = true;
        if (status.fn) key.fn = true;
        key.pressed = true;
        KeyStroke = key;
        if (Keyboard.isKeyPressed(',') || Keyboard.isKeyPressed(';')) PrevPress = true;
        if (Keyboard.isKeyPressed('`') || Keyboard.isKeyPressed(KEY_BACKSPACE)) EscPress = true;
        if (Keyboard.isKeyPressed('/') || Keyboard.isKeyPressed('.')) NextPress = true;
        if (Keyboard.isKeyPressed(KEY_ENTER)) SelPress = true;
        if (!KeyStroke.pressed) return;
        String keyStr = "";
        for (auto i : KeyStroke.word) {
            if (keyStr != "") {
                keyStr = keyStr + "+" + i;
            } else {
                keyStr += i;
            }
        }
        // Serial.println(keyStr);
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
