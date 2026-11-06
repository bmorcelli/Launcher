#include "hal/bright/bright.h"
#include "hal/inputs/buttons.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include <SD.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <interface.h>

namespace {
constexpr gpio_num_t userButton = GPIO_NUM_0;
constexpr gpio_num_t ioButton = GPIO_NUM_46;
constexpr gpio_num_t vext = GPIO_NUM_40;      // Active low: LCD, SD and touch pull-ups
constexpr gpio_num_t gnssPower = GPIO_NUM_42; // Active low
constexpr uint8_t touchAddress = 0x2e;
bool touchReady = false;

void output(gpio_num_t pin, int level) {
    gpio_set_level(pin, level);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_hold_dis(pin);
}

// CHSC6xxx normal mapped report. The controller returns native portrait coordinates.
bool readTouch(LTouchPoint &point) {
    Wire.beginTransmission(touchAddress);
    Wire.write(uint8_t{0});
    if (Wire.endTransmission() != 0) return false;
    uint8_t data[7];
    if (Wire.requestFrom(touchAddress, sizeof(data)) != sizeof(data)) return false;
    for (auto &byte : data) byte = Wire.read();
    const int count = data[2] & 7;
    const int event = data[3] >> 6;
    if (count == 0 || count > 2 || event == 1 || event == 3) return false;
    const int x = ((data[3] & 15) << 8) | data[4];
    const int y = ((data[5] & 15) << 8) | data[6];
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return false;

    switch (rotation & 3) {
        case 0:
            point.x = x;
            point.y = y;
            break;
        case 1:
            point.x = y;
            point.y = TFT_WIDTH - 1 - x;
            break;
        case 2:
            point.x = TFT_WIDTH - 1 - x;
            point.y = TFT_HEIGHT - 1 - y;
            break;
        case 3:
            point.x = TFT_HEIGHT - 1 - y;
            point.y = x;
            break;
    }
    point.pressed = true;
    return true;
}
} // namespace

void _setup_gpio() {
    gpio_deep_sleep_hold_dis();
    rtc_gpio_deinit(userButton);
    output(GPIO_NUM_2, LOW);  // RF front-end enable
    output(GPIO_NUM_7, LOW);  // RF front-end supply
    output(GPIO_NUM_8, HIGH); // SX1262 CS
    output(GPIO_NUM_4, LOW);  // buzzer
    output(gnssPower, HIGH);
    output(static_cast<gpio_num_t>(TFT_CS), HIGH);
    output(static_cast<gpio_num_t>(SDCARD_CS), HIGH);
    output(vext, LOW);
    gpio_hold_dis(static_cast<gpio_num_t>(TFT_BL));
    hal_bright_attach(TFT_BL);
    hal_buttons_init_2(DeviceButtons{userButton, ioButton}, 600, true);
    launcherDelayMs(100);
}

void _post_setup_gpio() {
    // The display init has already pulsed GPIO21, shared by the LCD and touch.
    if (Wire.begin(17, 18, 100000)) {
        Wire.setTimeOut(10);
        Wire.beginTransmission(touchAddress);
        touchReady = Wire.endTransmission() == 0;
    }
    launcherConsolePrintf("Heltec V2 touch: %s\n", touchReady ? "ready" : "unavailable");
}

void _setBrightness(uint8_t value) { hal_bright_set(TFT_BL, value); }

void InputHandler() {
    hal_buttons_poll_2();
    if (!touchReady) return;
    static uint32_t lastPoll = 0;
    static bool held = false;
    const uint32_t now = launcherMillis();
    if (now - lastPoll < 20) return;
    lastPoll = now;
    LTouchPoint point;
    bool down = readTouch(point);
    if (down && (!held || LongPress)) hal_touch_apply(point);
    held = down;
}

void powerOff() {
    const auto start = launcherMillis();
    while (!gpio_get_level(userButton)) {
        if (launcherMillis() - start >= 1500) return;
        launcherDelayMs(10);
    }
    if (esp_sleep_enable_ext0_wakeup(userButton, LOW) != ESP_OK) return;
    launcherInputLock(); // Park touch and button polling before removing power.
    _setBrightness(0);
    if (sdcardMounted) SDM.end();
    Wire.end();
    // Leave the shared bus high impedance before switching its supply off.
    for (int pin : {TFT_CS, SDCARD_CS, TFT_SCLK, TFT_MOSI, TFT_MISO, TFT_DC, TFT_RST, 17, 18}) {
        const auto gpio = static_cast<gpio_num_t>(pin);
        gpio_set_direction(gpio, GPIO_MODE_INPUT);
        gpio_set_pull_mode(gpio, GPIO_FLOATING);
    }
    output(vext, HIGH);
    esp_deep_sleep_start();
}
