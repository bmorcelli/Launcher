#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

#define SEL_BTN 0
#define UP_BTN 3
#define DW_BTN 15
#define L_BTN 2
#define R_BTN 1
#define BTN_ACT LOW
#define PIN_POWER_ON 10

// Setup for Trackball
void IRAM_ATTR ISR_up();
void IRAM_ATTR ISR_down();
void IRAM_ATTR ISR_left();
void IRAM_ATTR ISR_right();

volatile int8_t trackball_axis_x = 0;
volatile int8_t trackball_axis_y = 0;
volatile uint32_t trackball_axis_expiry_ms = 0;

#define TRACKBALL_AXIS_COOLDOWN_MS 250
#define TRACKBALL_AXIS_THRESHOLD 2

void IRAM_ATTR ISR_up() {
    trackball_axis_y > 0 ? trackball_axis_y = -1 : --trackball_axis_y;
    trackball_axis_expiry_ms = launcherMillis() + TRACKBALL_AXIS_COOLDOWN_MS;
}
void IRAM_ATTR ISR_down() {
    trackball_axis_y < 0 ? trackball_axis_y = 1 : ++trackball_axis_y;
    trackball_axis_expiry_ms = launcherMillis() + TRACKBALL_AXIS_COOLDOWN_MS;
}
void IRAM_ATTR ISR_left() {
    trackball_axis_x > 0 ? trackball_axis_x = -1 : --trackball_axis_x;
    trackball_axis_expiry_ms = launcherMillis() + TRACKBALL_AXIS_COOLDOWN_MS;
}
void IRAM_ATTR ISR_right() {
    trackball_axis_x < 0 ? trackball_axis_x = 1 : ++trackball_axis_x;
    trackball_axis_expiry_ms = launcherMillis() + TRACKBALL_AXIS_COOLDOWN_MS;
}

void ISR_rst() {
    trackball_axis_x = 0;
    trackball_axis_y = 0;
    trackball_axis_expiry_ms = 0;
}

#define LILYGO_KB_SLAVE_ADDRESS 0x55
#define LILYGO_KB_BRIGHTNESS_CMD 0x01
#define KB_I2C_SDA 18
#define KB_I2C_SCL 8
#define SEL_BTN 0
#define UP_BTN 3
#define DW_BTN 15
#define L_BTN 1
#define R_BTN 2
#define PIN_POWER_ON 10
#define BOARD_TOUCH_INT 16

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_irq = BOARD_TOUCH_INT;
#ifdef T_DECK_PLUS
    constexpr bool isPlus = true;
#else
    constexpr bool isPlus = false;
#endif
    // rotation:          0         1        2         3
    bool swapXY[4] = {false, true, false, true};
    bool mirrorX[4] = {false, !isPlus, true, isPlus};
    bool mirrorY[4] = {!isPlus, true, isPlus, false};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherDelayMs(500); // time to ESP32C3 start and enable the keyboard
    if (!Wire.begin(KB_I2C_SDA, KB_I2C_SCL))
        launcherConsolePrintf("%s\n", String("Fail starting ESP32-C3 keyboard").c_str());

    launcherGpioOutput(PIN_POWER_ON);
    launcherGpioWrite(PIN_POWER_ON, HIGH);
    launcherGpioInput(SEL_BTN);
    launcherGpioInput(BOARD_TOUCH_INT);
    if (!hal_touch_init(touchCfg())) {
        launcherConsolePrintf("%s\n", String("Failed to find GT911 - check your wiring!").c_str());
    }

    launcherGpioOutput(9); // LoRa Radio CS Pin to HIGH (Inhibit the SPI Communication for this module)
    launcherGpioWrite(9, HIGH);

    // Setup for Trackball
    launcherGpioInputPullup(UP_BTN);
    attachInterrupt(UP_BTN, ISR_up, FALLING);
    launcherGpioInputPullup(DW_BTN);
    attachInterrupt(DW_BTN, ISR_down, FALLING);
    launcherGpioInputPullup(L_BTN);
    attachInterrupt(L_BTN, ISR_left, FALLING);
    launcherGpioInputPullup(R_BTN);
    attachInterrupt(R_BTN, ISR_right, FALLING);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
// uint8_t isPlus = false;
void _post_setup_gpio() {}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    Wire.beginTransmission(LILYGO_KB_SLAVE_ADDRESS);
    Wire.write(LILYGO_KB_BRIGHTNESS_CMD);
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
    } else {
        const uint8_t PWM_MIN = 85;
        const uint8_t PWM_MAX = 255;
        float linear = (float)brightval / 100.0;
        uint8_t value = PWM_MIN + round(pow(linear, 2.2) * (PWM_MAX - PWM_MIN));
        analogWrite(TFT_BL, value);
    }
    Wire.endTransmission();
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    char keyValue = 0;
    static unsigned long tm = launcherMillis();
    LTouchPoint t;
    bool touched = hal_touch_read(touchCfg(), t);
    launcherDelayMs(2);
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    while (Wire.available() > 0) {
        keyValue = Wire.read();
        launcherDelayMs(1);
    }

    if (keyValue != (char)0x00) {
        if (!wakeUpScreen()) {
            AnyKeyPress = true;
        } else return;
        KeyStroke.Clear();
        KeyStroke.hid_keys.push_back(keyValue);
        if (keyValue == ' ') KeyStroke.exit_key = true; // key pressed to try to exit
        if (keyValue == (char)0x08) {
            KeyStroke.exit_key = true;
            KeyStroke.del = true;
        }

        if (keyValue == 'w') UpPress = true;
        if (keyValue == 's') DownPress = true;
        if (keyValue == 'a') PrevPress = true;
        if (keyValue == 'd') NextPress = true;

        if (keyValue == (char)0x0D) KeyStroke.enter = true;
        if (launcherGpioRead(SEL_BTN) == BTN_ACT) KeyStroke.fn = true;
        KeyStroke.word.push_back(keyValue);
        if (KeyStroke.del) EscPress = true;
        if (KeyStroke.enter) SelPress = true;
        KeyStroke.pressed = true;
        tm = launcherMillis();
    } else KeyStroke.pressed = false;

    if (launcherMillis() - tm < 200 && !LongPress) return;

    // if the trackball movement has expired, reset it to avoid unwanted movements
    if (trackball_axis_expiry_ms && trackball_axis_expiry_ms <= launcherMillis()) { ISR_rst(); }

    if (abs(trackball_axis_x) >= TRACKBALL_AXIS_THRESHOLD ||
        abs(trackball_axis_y) >= TRACKBALL_AXIS_THRESHOLD) {

        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;

        // launcherConsolePrintf("%s", String("Trackball: [").c_str());
        // launcherConsolePrintf("%s", String(trackball_axis_x).c_str()); launcherConsolePrintf("%s",
        // String(", ").c_str()); launcherConsolePrintf("%s", String(trackball_axis_y).c_str());
        // launcherConsolePrintf("%s\n", String("]").c_str());
        if (trackball_axis_x < 0) {
            ISR_rst();
            PrevPress = true;
        } // Up
        else if (trackball_axis_x > 0) {
            ISR_rst();
            NextPress = true;
        } // Down
        if (trackball_axis_y < 0) {
            ISR_rst();
            UpPress = true;
        } // Up
        else if (trackball_axis_y > 0) {
            ISR_rst();
            DownPress = true;
        } // Down
    }

    if (launcherGpioRead(SEL_BTN) == BTN_ACT) {
        tm = launcherMillis();
        if (!wakeUpScreen()) {
            AnyKeyPress = true;
        } else return;
        SelPress = true;
    }

    if ((launcherMillis() - tm) > 190 || LongPress) { // one reading each 190ms
        if (touched) {

            // launcherConsolePrintf("\nPressed x=%d , y=%d, rot: %d", t.x, t.y, rotation);
            tm = launcherMillis();
            if (!hal_touch_apply(t)) return;
            touched = 0;
            return;
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    launcherGpioWrite(PIN_POWER_ON, LOW);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
