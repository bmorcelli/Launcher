#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint8_t expanderAddress = 0x24;
constexpr uint8_t outputRegister = 0x03;
constexpr uint8_t bootButton = 9;
bool sdBusReady = false;

bool writeExpander(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(expanderAddress);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}
} // namespace

bool launcherSdBusBegin() {
    sdBusReady = writeExpander(outputRegister, 0xf7);
    if (!sdBusReady) Serial.println("Failed to disconnect display from SD bus");
    if (sdBusReady) delay(10);
    return sdBusReady;
}

bool launcherSdChipSelect(bool high) {
    if (!sdBusReady) return false;
    sdBusReady = writeExpander(outputRegister, high ? 0xf7 : 0xf3);
    if (!sdBusReady) Serial.println("Failed to switch SD chip select");
    if (sdBusReady) delay(10);
    return sdBusReady;
}

void launcherSdBusEnd() {
    if (!writeExpander(outputRegister, 0xf7) || !writeExpander(outputRegister, 0xf6))
        Serial.println("Failed to restore display on SD bus");
    sdBusReady = false;
}

void _setup_gpio() {
    Wire.begin(4, 3);
    Wire.setClock(400000);
    if (!writeExpander(0x02, 0xff) || !writeExpander(outputRegister, 0xf6)) {
        Serial.println("Failed to initialize display expander");
    }
    delay(20);
    writeExpander(outputRegister, 0xf4);
    delay(20);
    writeExpander(outputRegister, 0xf6);
    delay(120);
    hal_buttons_init(DeviceButtons{bootButton}, 1);
}

void InputHandler() { hal_buttons_poll_1(DeviceButtons{bootButton}); }

void _setBrightness(uint8_t brightness) {
    if (!writeExpander(0x05, (uint16_t)brightness * 255 / 100))
        Serial.println("Failed to set display brightness");
}

void powerOff() {
    _setBrightness(0);
    esp_deep_sleep_start();
}
