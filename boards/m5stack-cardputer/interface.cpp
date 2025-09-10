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
void mapRawKeyToPhysical(uint8_t rawValue, uint8_t &row, uint8_t &col) {
    switch (rawValue) {
        case 1:
            row = 0;
            col = 0;
            break; // ESC/`
        case 2:
            row = 1;
            col = 0;
            break; // Tab
        case 3:
            row = 2;
            col = 0;
            break; // FN
        case 4:
            row = 3;
            col = 0;
            break; // Ctrl
        case 5:
            row = 0;
            col = 1;
            break; // 1
        case 6:
            row = 1;
            col = 1;
            break; // Q
        case 7:
            row = 2;
            col = 1;
            break; // Shift
        case 8:
            row = 3;
            col = 1;
            break; // Opt
        case 11:
            row = 0;
            col = 2;
            break; // 2
        case 12:
            row = 1;
            col = 2;
            break; // W
        case 13:
            row = 2;
            col = 2;
            break; // A
        case 14:
            row = 3;
            col = 2;
            break; // Alt
        case 15:
            row = 0;
            col = 3;
            break; // 3
        case 16:
            row = 1;
            col = 3;
            break; // E
        case 17:
            row = 2;
            col = 3;
            break; // S
        case 18:
            row = 3;
            col = 3;
            break; // Z
        case 21:
            row = 0;
            col = 4;
            break; // 4
        case 22:
            row = 1;
            col = 4;
            break; // R
        case 23:
            row = 2;
            col = 4;
            break; // D
        case 24:
            row = 3;
            col = 4;
            break; // X
        case 25:
            row = 0;
            col = 5;
            break; // 5
        case 26:
            row = 1;
            col = 5;
            break; // T
        case 27:
            row = 2;
            col = 5;
            break; // F
        case 28:
            row = 3;
            col = 5;
            break; // C
        case 31:
            row = 0;
            col = 6;
            break; // 6
        case 32:
            row = 1;
            col = 6;
            break; // Y
        case 33:
            row = 2;
            col = 6;
            break; // G
        case 34:
            row = 3;
            col = 6;
            break; // V
        case 35:
            row = 0;
            col = 7;
            break; // 7
        case 36:
            row = 1;
            col = 7;
            break; // U
        case 37:
            row = 2;
            col = 7;
            break; // H
        case 38:
            row = 3;
            col = 7;
            break; // B
        case 41:
            row = 0;
            col = 8;
            break; // 8
        case 42:
            row = 1;
            col = 8;
            break; // I
        case 43:
            row = 2;
            col = 8;
            break; // J
        case 44:
            row = 3;
            col = 8;
            break; // N
        case 45:
            row = 0;
            col = 9;
            break; // 9
        case 46:
            row = 1;
            col = 9;
            break; // O
        case 47:
            row = 2;
            col = 9;
            break; // K
        case 48:
            row = 3;
            col = 9;
            break; // M
        case 51:
            row = 0;
            col = 10;
            break; // 0
        case 52:
            row = 1;
            col = 10;
            break; // P
        case 53:
            row = 2;
            col = 10;
            break; // L
        case 54:
            row = 3;
            col = 10;
            break; // ,
        case 55:
            row = 0;
            col = 11;
            break; // -
        case 56:
            row = 1;
            col = 11;
            break; // [
        case 57:
            row = 2;
            col = 11;
            break; // ;
        case 58:
            row = 3;
            col = 11;
            break; // .
        case 61:
            row = 0;
            col = 12;
            break; // =
        case 62:
            row = 1;
            col = 12;
            break; // ]
        case 63:
            row = 2;
            col = 12;
            break; // '
        case 64:
            row = 3;
            col = 12;
            break; // /
        case 65:
            row = 0;
            col = 13;
            break; // Backspace
        case 66:
            row = 1;
            col = 13;
            break; //
        case 67:
            row = 2;
            col = 13;
            break; // Enter
        case 68:
            row = 3;
            col = 13;
            break; // Space
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
    bool UseTCA8418 = tca.begin(TCA8418_I2C_ADDR, &Wire);

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
    // Rows 0-3 as outputs, columns 4-17 as inputs
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

    bool shoulder = digitalRead(0);

    if (UseTCA8418) {
        // Poll TCA8418 every 100ms for key events
        if (millis() - lastCheck > 100) {
            lastCheck = millis();
            if (tca.available() > 0) {
                int keyEvent = tca.getEvent();
                bool pressed = !(keyEvent & 0x80); // Bit 7: 0=pressed, 1=released
                uint8_t value = keyEvent & 0x7F;   // Bits 0-6: key value

                // Debounce check
                if (millis() - lastKeyTime < 50 && value == lastKeyValue) { return; }
                lastKeyTime = millis();
                lastKeyValue = value;

                // Map raw value to physical position
                uint8_t row, col;
                mapRawKeyToPhysical(value, row, col);

                Serial.printf("Key event: raw=%d, pressed=%d, row=%d, col=%d\n", value, pressed, row, col);

                if (row >= 4 || col >= 14) {
                    Serial.printf("Invalid position: row=%d, col=%d\n", row, col);
                    return;
                }

                if (wakeUpScreen()) return;

                AnyKeyPress = true;

                if (handleSpecialKeys(row, col, pressed) > 0) return;

                if (pressed) {
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
                    KeyStroke.Clear();
                    LongPressTmp = false;
                }
            }

        } else {
            Keyboard.update();
            if (millis() - tm > 200 || LongPress) {
                if (Keyboard.isPressed()) {
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
                    // if(Keyboard.isKeyPressed('/'))                                          NextPagePress =
                    // true;
                    // // right arrow if(Keyboard.isKeyPressed(',')) PrevPagePress = true;  // left arrow
                    if (KeyStroke.pressed) {
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
                } else {
                    KeyStroke.Clear();
                    LongPressTmp = false;
                }
            }
        }
    }
    if (shoulder == LOW) { // GPIO0 button for ADV
        tm = millis();
        bool screenWasOff = wakeUpScreen();
        if (!screenWasOff) yield();

        // Always set SelPress for GPIO0 button on Cardputer ADV
        SelPress = true;
        AnyKeyPress = true;
        wakeUpScreen(); // Ensure power save timer is reset
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
