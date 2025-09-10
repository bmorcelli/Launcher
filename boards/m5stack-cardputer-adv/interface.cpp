// Cardputer ADV Support by n0xa 
#include "powerSave.h"
#include <interface.h>

#ifdef CARDPUTER_ADV
#include <Adafruit_TCA8418.h>
#include <Wire.h>

// TCA8418 keyboard controller for ADV variant
Adafruit_TCA8418 tca;
#else
#include <Keyboard.h>
Keyboard_Class Keyboard;
#endif

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    //    Keyboard.begin();
    pinMode(0, INPUT);
    pinMode(10, INPUT); // Pin that reads the
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH); // Set GPIO5 HIGH for SD card compatibility (thx for the tip @bmorcelli & 7h30th3r0n3)
}

void _post_setup_gpio() { 
#ifdef CARDPUTER_ADV
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
    bool tca_found = tca.begin(TCA8418_I2C_ADDR, &Wire);
    
    if (!tca_found) {
        Serial.println("ERROR: Failed to initialize TCA8418!");
        return;
    }
    
    Serial.println("DEBUG: TCA8418 found and initialized successfully!");
    
    // Configure the matrix (7 rows x 8 columns)
    Serial.println("DEBUG: Configuring TCA8418 matrix (7x8)");
    tca.matrix(7, 8);
    
    Serial.println("DEBUG: TCA8418 configured for polling mode (interrupts disabled)");
#else
    Keyboard.begin(); 
#endif
}
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <soc/adc_channel.h>
#include <soc/soc_caps.h>
/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    uint8_t percent;
    uint8_t _batAdcCh = ADC1_GPIO10_CHANNEL;
    uint8_t _batAdcUnit = 1;

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten((adc1_channel_t)_batAdcCh, ADC_ATTEN_DB_12);
    static esp_adc_cal_characteristics_t *adc_chars = nullptr;
    static constexpr int BASE_VOLATAGE = 3600;
    adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(
        (adc_unit_t)_batAdcUnit, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, BASE_VOLATAGE, adc_chars
    );
    int raw;
    raw = adc1_get_raw((adc1_channel_t)_batAdcCh);
    uint32_t volt = esp_adc_cal_raw_to_voltage(raw, adc_chars);

    float mv = volt * 2;
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
    static long tmp = 0;
#ifdef CARDPUTER_ADV
    static unsigned long lastCheck = 0;
    
    // Poll TCA8418 every 100ms for key events
    if (millis() - lastCheck > 100) {
        lastCheck = millis();
        
        // Check if there are any events available
        if (tca.available()) {
            while (tca.available()) {
                int keycode = tca.getEvent();
                if (keycode > 0) {
                    // Extract row and column from keycode
                    int row = (keycode & 0x70) >> 4; // Bits 4-6 for row
                    int col = keycode & 0x0F;        // Bits 0-3 for column
                    bool pressed = !(keycode & 0x80); // Bit 7 for press/release
                    
                    Serial.printf("TCA8418 Key Event: Row=%d, Col=%d, %s (keycode=0x%02X)\n", 
                                  row, col, pressed ? "PRESSED" : "RELEASED", keycode);
                    
                    // Map navigation keys based on discovered coordinates
                    if (pressed) {
                        // Navigation key mapping for Cardputer ADV (matching original logic)
                        if (row == 4 && col == 3) { // Enter key
                            SelPress = true;
                            Serial.println("MAPPED: Enter/Select pressed");
                        } else if (row == 3 && col == 9 || row == 3 && col == 6) { // Up arrow OR Left arrow (Previous)
                            PrevPress = true;
                            Serial.println("MAPPED: Up/Left/Previous pressed");
                        } else if (row == 3 && col == 10 || row == 4 && col == 0) { // Down arrow OR Right arrow (Next)  
                            NextPress = true;
                            Serial.println("MAPPED: Down/Right/Next pressed");
                        } else if (row == 0 && col == 1) { // Escape key
                            EscPress = true;
                            Serial.println("MAPPED: Escape pressed");
                        }
                        
                        AnyKeyPress = true;
                        wakeUpScreen(); // Reset power save timer on key press
                    }
                }
            }
        }
    }
#else
    Keyboard.update();
#endif
    if (millis() - tmp > 200 || LongPress) {
#ifdef CARDPUTER_ADV
        if (digitalRead(0) == LOW) { // GPIO0 button for ADV
            tmp = millis();
            bool screenWasOff = wakeUpScreen();
            if (!screenWasOff) yield();
            
            // Always set SelPress for GPIO0 button on Cardputer ADV
            SelPress = true;
            AnyKeyPress = true;
            wakeUpScreen(); // Ensure power save timer is reset
        }
#else
        if (Keyboard.isPressed() || digitalRead(0) == LOW) {
            tmp = millis();
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
            if (Keyboard.isKeyPressed(KEY_ENTER) || digitalRead(0) == LOW) SelPress = true;
            // if(Keyboard.isKeyPressed('/'))                                          NextPagePress = true;
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
#endif
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
