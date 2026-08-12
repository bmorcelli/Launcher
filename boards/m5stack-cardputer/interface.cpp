#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Adafruit_TCA8418.h>
#include <Keyboard.h>
#include <Wire.h>
#include <interface.h>

#define TCA8418_INT_PIN 11
#define TCA8418_I2C_ADDR 0x34
#define TCA8418_SDA_PIN 8
#define TCA8418_SCL_PIN 9
// Cardputer and 1.1 keyboard
Keyboard_Class Keyboard;
// TCA8418 keyboard controller for ADV variant
Adafruit_TCA8418 tca;
bool UseTCA8418 = false; // Set to true to use TCA8418 (Cardputer ADV)

// Keyboard state variables
bool fn_key_pressed = false;
bool shift_key_pressed = false;
bool caps_lock = false;

constexpr unsigned long TCA8418_REPEAT_START_MS = 350;
constexpr unsigned long TCA8418_REPEAT_MS = 150;

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
            if (fn_key_pressed) launcherConsolePrintf("%s\n", String("FN Pressed").c_str());
            else launcherConsolePrintf("%s\n", String("FN Released").c_str());
            return 1;
        case 0x81:
            shift_key_pressed = pressed;
            if (shift_key_pressed) launcherConsolePrintf("%s\n", String("Shift Pressed").c_str());
            else launcherConsolePrintf("%s\n", String("Shift Released").c_str());
            if (shift_key_pressed && fn_key_pressed) {
                caps_lock = !caps_lock;
                if (caps_lock) launcherConsolePrintf("%s\n", String("CAPS Lock activated").c_str());
                else launcherConsolePrintf("%s\n", String("CAPS Lock DEactivated").c_str());
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
    launcherGpioInput(0);
    launcherGpioInput(10); // Pin that reads the Battery voltage
    launcherGpioOutput(3);
    launcherGpioOutput(4);
    launcherGpioOutput(5);
    launcherGpioOutput(6);
    launcherGpioOutput(13);
    launcherGpioOutput(15);
    // Set GPIO5 HIGH for SD card compatibility (thx for the tip @bmorcelli & 7h30th3r0n3)
    launcherGpioWrite(3, HIGH);
    launcherGpioWrite(4, HIGH);
    launcherGpioWrite(5, HIGH);
    launcherGpioWrite(6, HIGH);
    launcherGpioWrite(13, HIGH);
    launcherGpioWrite(15, HIGH);
}
volatile bool kb_interrupt = false;
void IRAM_ATTR gpio_isr_handler(void *arg) { kb_interrupt = true; }
void _post_setup_gpio() {
    // Initialize TCA8418 I2C keyboard controller
    launcherConsolePrintf("%s\n", String("DEBUG: Cardputer ADV - Initializing TCA8418 keyboard").c_str());

    // Use correct I2C pins for Cardputer ADV
    launcherConsolePrintf("DEBUG: Initializing I2C with SDA=%d, SCL=%d\n", TCA8418_SDA_PIN, TCA8418_SCL_PIN);
    Wire.begin(TCA8418_SDA_PIN, TCA8418_SCL_PIN);
    launcherDelayMs(100);

    // Scan I2C bus to see what's available
    launcherConsolePrintf("%s\n", String("DEBUG: Scanning I2C bus...").c_str());
    byte found_devices = 0;
    for (byte i = 1; i < 127; i++) {
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0) {
            launcherConsolePrintf("DEBUG: Found I2C device at address 0x%02X\n", i);
            found_devices++;
        }
    }
    launcherConsolePrintf("DEBUG: Found %d I2C devices\n", found_devices);

    // Try to initialize TCA8418
    launcherConsolePrintf("DEBUG: Attempting to initialize TCA8418 at address 0x%02X\n", TCA8418_I2C_ADDR);
    UseTCA8418 = tca.begin(TCA8418_I2C_ADDR, &Wire);

    if (!UseTCA8418) {
        launcherConsolePrintf("%s\n", String("ADV  : Failed to initialize TCA8418!").c_str());
        launcherConsolePrintf(
            "%s\n", String("Probable standard Cardputer detected, switching to Keyboard library").c_str()
        );
        Wire.end();
        Keyboard.begin();
        return;
    }

    tca.matrix(7, 8);
    tca.flush();
    launcherGpioInput(11);
    // TCA8418 INT is active-low; only the falling edge means "event available".
    // FALLING avoids the spurious rising-edge ISR that fired after we cleared
    // INT_STAT, which was resetting the sel/esc hold state one frame too early.
    attachInterruptArg(digitalPinToInterrupt(11), gpio_isr_handler, nullptr, FALLING);
    tca.enableInterrupts();
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    static unsigned long nextRepeatTime = 0;
    static unsigned long prevRepeatTime = 0;
    static unsigned long upRepeatTime = 0;
    static unsigned long downRepeatTime = 0;

    static bool sel = false;
    static bool prev = false;
    static bool next = false;
    static bool up = false;
    static bool down = false;
    static bool esc = false;

    if (!UseTCA8418 && launcherMillis() - tm < 200 && !LongPress) return;

    if (launcherGpioRead(0) == LOW) { // GPIO0 button, shoulder button
        tm = launcherMillis();
        AnyKeyPress = true;
        if (!wakeUpScreen()) yield();
        else return;
        SelPress = true;
        AnyKeyPress = true;
    }
    if (UseTCA8418) {
        bool keyEventHandled = false;
        bool nextPulse = false;
        bool prevPulse = false;
        bool upPulse = false;
        bool downPulse = false;
        bool delPulse = false;
        bool keyPulse = false;
        keyStroke pendingKey;

        if (kb_interrupt) {
            // Drain the FIFO now. Processing one TCA8418 event per 200 ms made quick taps
            // pile up and replay later as delayed navigation.
            bool wokeScreen = false;
            while (tca.available() > 0) {
                int keyEvent = tca.getEvent();
                bool pressed = (keyEvent & 0x80); // Bit 7: 1 Pressed, 0 Released
                uint8_t value = keyEvent & 0x7F;  // Bits 0-6: key value

                // Map raw value to physical position
                uint8_t row, col;
                mapRawKeyToPhysical(value, row, col);

                // launcherConsolePrintf("Key event: raw=%d, pressed=%d, row=%d, col=%d\n", value, pressed,
                // row, col);

                if (row >= 4 || col >= 14) continue;

                // wakeUpScreen() is stateful — it returns true only on the first call when
                // the screen is actually sleeping. Track the result so that all remaining
                // events in the same FIFO burst are also discarded, not just the first one.
                if (!wokeScreen) wokeScreen = wakeUpScreen();
                if (wokeScreen) continue;

                AnyKeyPress = true;
                keyEventHandled = true;

                if (handleSpecialKeys(row, col, pressed) > 0) continue;

                if (!pressed) {
                    KeyStroke.Clear();
                    LongPressTmp = false;
                }

                char keyVal = getKeyChar(row, col);

                // launcherConsolePrintf("Key pressed: %c (0x%02X) at row=%d, col=%d\n", keyVal, keyVal, row,
                // col);

                if (keyVal == KEY_BACKSPACE && col == 13) {
                    if (pressed) {
                        delPulse = true;
                        esc = true;
                    } else {
                        esc = false;
                    }
                } else if (keyVal == '`') {
                    esc = pressed;
                    if (pressed) {
                        pendingKey.word.emplace_back(keyVal);
                        keyPulse = true;
                    }
                } else if (keyVal == KEY_ENTER && col == 13) {
                    sel = pressed;
                    if (pressed) {
                        pendingKey.enter = true;
                        pendingKey.word.emplace_back(KEY_ENTER);
                        keyPulse = true;
                    }
                } else if (keyVal == ';') {
                    up = pressed;
                    if (pressed) {
                        upPulse = true;
                        upRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
                        pendingKey.word.emplace_back(keyVal);
                        keyPulse = true;
                    }
                } else if (keyVal == ',') {
                    prev = pressed;
                    if (pressed) {
                        prevPulse = true;
                        prevRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
                        pendingKey.word.emplace_back(keyVal);
                        keyPulse = true;
                    }
                } else if (keyVal == '.') {
                    down = pressed;
                    if (pressed) {
                        downPulse = true;
                        downRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
                        pendingKey.word.emplace_back(keyVal);
                        keyPulse = true;
                    }
                } else if (keyVal == '/') {
                    next = pressed;
                    if (pressed) {
                        nextPulse = true;
                        nextRepeatTime = launcherMillis() + TCA8418_REPEAT_START_MS;
                        pendingKey.word.emplace_back(keyVal);
                        keyPulse = true;
                    }
                } else if (keyVal == KEY_TAB) {
                    if (pressed) {
                        pendingKey.word.emplace_back(KEY_TAB);
                        keyPulse = true;
                    }
                } else if (keyVal == 0xFF) {
                    if (pressed) {
                        pendingKey.fn = true;
                        keyPulse = true;
                    }
                } else if (keyVal == KEY_LEFT_SHIFT) {
                    if (pressed) {
                        pendingKey.modifier_keys.emplace_back(KEY_LEFT_SHIFT);
                        keyPulse = true;
                    }
                } else if (keyVal == KEY_LEFT_CTRL) {
                    if (pressed) {
                        pendingKey.modifier_keys.emplace_back(KEY_LEFT_CTRL);
                        keyPulse = true;
                    }
                } else if (keyVal == KEY_LEFT_ALT) {
                    if (pressed) {
                        pendingKey.modifier_keys.emplace_back(KEY_LEFT_ALT);
                        keyPulse = true;
                    }
                } else {
                    if (pressed) {
                        pendingKey.word.emplace_back(keyVal);
                        keyPulse = true;
                    }
                }
            }

            //  try to clear the IRQ flag
            //  if there are pending events it is not cleared
            tca.writeRegister(TCA8418_REG_INT_STAT, 1);
            int intstat = tca.readRegister(TCA8418_REG_INT_STAT);
            if ((intstat & 0x01) == 0) { kb_interrupt = false; }
        }

        unsigned long now = launcherMillis();
        if (next && now >= nextRepeatTime) {
            nextPulse = true;
            nextRepeatTime = now + TCA8418_REPEAT_MS;
        }
        if (prev && now >= prevRepeatTime) {
            prevPulse = true;
            prevRepeatTime = now + TCA8418_REPEAT_MS;
        }
        if (up && now >= upRepeatTime) {
            upPulse = true;
            upRepeatTime = now + TCA8418_REPEAT_MS;
        }
        if (down && now >= downRepeatTime) {
            downPulse = true;
            downRepeatTime = now + TCA8418_REPEAT_MS;
        }

        if (!keyEventHandled && !nextPulse && !prevPulse && !upPulse && !downPulse && !LongPress) {
            sel = false; // avoid multiple selections
            esc = false; // avoid multiple escapes
        }
        if (delPulse) {
            pendingKey.del = true;
            pendingKey.exit_key = true;
            keyPulse = true;
        }
        if (keyPulse) {
            pendingKey.pressed = true;
            KeyStroke = pendingKey;
        } else if (!nextPulse && !prevPulse) {
            KeyStroke.Clear();
        }
        if (nextPulse || prevPulse || keyPulse) AnyKeyPress = true;

        NextPress = nextPulse;
        PrevPress = prevPulse;
        UpPress = upPulse;
        DownPress = downPulse;
        SelPress = sel | SelPress; // in case G0 is pressed
        EscPress = esc;
        tm = now;
        return;
    } else {
        Keyboard.update();
        if (!Keyboard.isPressed()) {
            KeyStroke.Clear();
            LongPressTmp = false;
            return;
        }
        tm = launcherMillis();
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
        // launcherConsolePrintf("%s\n", String(keyStr).c_str());
    }
}

void reboot() {
    launcherConsolePrintf("%s", String("\r\n").c_str());
    launcherConsoleFlush();
    launcherConsoleEnd();
    vTaskDelay(pdMS_TO_TICKS(50));
    launcherGpioInput(1);
    launcherGpioInput(2);
    launcherGpioInput(13);
    launcherGpioInput(15);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP.restart();
}
