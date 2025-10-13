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

int handleSpecialKeys(uint8_t row, uint8_t col, bool pressed);
void mapRawKeyToPhysical(uint8_t rawValue, uint8_t &row, uint8_t &col);

char getKeyChar(uint8_t row, uint8_t col) {
    char keyVal;
    if (shift_key_pressed ^ caps_lock) {
        keyVal = _key_value_map[row][col].value_second;
    } else {
        keyVal = _key_value_map[row][col].value_first;
    }
    return keyVal;
}

int handleSpecialKeys(uint8_t row, uint8_t col, bool pressed) {
    char keyVal = _key_value_map[row][col].value_first;
    switch (keyVal) {
        case 0xFF:
            fn_key_pressed = pressed;
            if (fn_key_pressed) Serial.println("FN Pressed");
            else Serial.println("FN Released");
            return 1;
        case 0x81:
            shift_key_pressed = pressed;
            if (shift_key_pressed) Serial.println("Shift Pressed");
            else Serial.println("Shift Released");
            if (shift_key_pressed && fn_key_pressed) {
                caps_lock = !caps_lock;
                if (caps_lock) Serial.println("CAPS Lock activated");
                else Serial.println("CAPS Lock DEactivated");
                shift_key_pressed = false;
                fn_key_pressed = false;
            }
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
bool kb_interrupt = false;
void IRAM_ATTR gpio_isr_handler(void *arg) { kb_interrupt = true; }
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

    tca.matrix(7, 8);
    tca.flush();
    pinMode(11, INPUT);
    attachInterruptArg(digitalPinToInterrupt(11), gpio_isr_handler, &kb_interrupt, CHANGE);
    tca.enableInterrupts();
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

    static bool sel = false;
    static bool prev = false;
    static bool next = false;
    static bool up = false;
    static bool down = false;
    static bool esc = false;
    static bool del = false;

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
        if (!kb_interrupt) {
            if (!LongPress) {
                sel = false; // avoid multiple selections
                esc = false; // avoid multiple escapes
            }
            NextPress = next;
            PrevPress = prev;
            UpPress = up;
            DownPress = down;
            SelPress = sel | SelPress; // in case G0 is pressed
            EscPress = esc;
            if (del) {
                KeyStroke.del = del;
                KeyStroke.pressed = true;
            }
            tm = millis();
            return;
        }

        //  try to clear the IRQ flag
        //  if there are pending events it is not cleared
        tca.writeRegister(TCA8418_REG_INT_STAT, 1);
        int intstat = tca.readRegister(TCA8418_REG_INT_STAT);
        if ((intstat & 0x01) == 0) { kb_interrupt = false; }

        if (tca.available() <= 0) return;
        int keyEvent = tca.getEvent();
        bool pressed = (keyEvent & 0x80); // Bit 7: 1 Pressed, 0 Released
        uint8_t value = keyEvent & 0x7F;  // Bits 0-6: key value

        // Map raw value to physical position
        uint8_t row, col;
        mapRawKeyToPhysical(value, row, col);

        // Serial.printf("Key event: raw=%d, pressed=%d, row=%d, col=%d\n", value, pressed, row, col);

        if (row >= 4 || col >= 14) return;

        if (wakeUpScreen()) return;

        AnyKeyPress = true;

        if (handleSpecialKeys(row, col, pressed) > 0) return;

        if (!pressed) {
            KeyStroke.Clear();
            LongPressTmp = false;
        }

        keyStroke key;
        char keyVal = getKeyChar(row, col);

        // Serial.printf("Key pressed: %c (0x%02X) at row=%d, col=%d\n", keyVal, keyVal, row, col);

        if (keyVal == KEY_BACKSPACE) {
            del = pressed;
            esc = pressed;
            // if (pressed) key.word.emplace_back(KEY_BACKSPACE);
        } else if (keyVal == '`') {
            esc = pressed;
            if (pressed) key.word.emplace_back(keyVal);
        } else if (keyVal == KEY_ENTER) {
            key.enter = pressed;
            if (pressed) key.word.emplace_back(KEY_ENTER);
            sel = pressed;
        } else if (keyVal == ',' || keyVal == ';') {
            prev = pressed;
            if (pressed) key.word.emplace_back(keyVal);
        } else if (keyVal == '/' || keyVal == '.') {
            next = pressed;
            if (pressed) key.word.emplace_back(keyVal);
        } else if (keyVal == KEY_TAB) {
            if (pressed) key.word.emplace_back(KEY_TAB);
        } else if (keyVal == 0xFF) {
            key.fn = pressed;
        } else if (keyVal == KEY_LEFT_SHIFT) {
            if (pressed) key.modifier_keys.emplace_back(KEY_LEFT_SHIFT);
        } else if (keyVal == KEY_LEFT_CTRL) {
            if (pressed) key.modifier_keys.emplace_back(KEY_LEFT_CTRL);
        } else if (keyVal == KEY_LEFT_ALT) {
            if (pressed) key.modifier_keys.emplace_back(KEY_LEFT_ALT);
        } else {
            if (pressed) key.word.emplace_back(keyVal);
        }
        key.pressed = pressed;
        if (del) {
            key.del = del;
            key.pressed = true;
        }
        KeyStroke = key;
        NextPress = next;
        PrevPress = prev;
        UpPress = up;
        DownPress = down;
        SelPress = sel;
        EscPress = esc;
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
        AnyKeyPress = true;

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
