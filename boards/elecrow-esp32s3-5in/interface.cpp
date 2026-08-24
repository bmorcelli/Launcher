#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

#define TOUCH_SDA_PIN GT911_I2C_CONFIG_SDA_IO_NUM
#define TOUCH_SCL_PIN GT911_I2C_CONFIG_SCL_IO_NUM
#define TOUCH_ADDR 0x5D // GT911 default I2C address
// GT911's INT pin (touch controller interrupt), shared with the ESP32 as a
// plain GPIO. Elecrow's own factory firmware holds it low across the GT911
// power-on window on every boot -- this is what actually makes the touch
// controller answer correctly, not just I2C address selection.
#define TOUCH_INT_PIN 1

// Elecrow shipped four hardware revisions of this board (V1.0-V1.3) that
// disagree on how backlight/touch power is sequenced. Per Elecrow's own
// factory firmware for each revision:
//  - V1.0: a TCA9534 I2C IO expander at 0x18 drives touch reset (its pin 2)
//    and other peripherals; backlight is driven directly off GPIO2 via PWM
//    (unverified here -- no hardware to test against; taken from the
//    commented-out cfg.pin_bl block Elecrow leaves in every LovyanGFX
//    example, V1.2's included).
//  - V1.1: backlight is an onboard STC8H1K28 power MCU at I2C address 0x30,
//    written 0x05 (off) .. 0x10 (max).
//  - V1.2/V1.3: same STC8H1K28 at 0x30, but 0-245 (0 = max, 245 = off) --
//    the expander is gone; touch reset is done with the INT-low trick alone.
// V1.1 vs V1.2/V1.3 both show up as "0x30 present" on a bus scan with no
// way to tell them apart electrically, so a detected STC8H1K28 is treated
// as V1.2/V1.3 (the current mainline hardware); a real V1.1 unit would need
// BACKLIGHT_V1_1_PROTOCOL below flipped on by hand.
#define TCA9534_ADDR 0x18
#define BACKLIGHT_I2C_ADDR 0x30
#define BACKLIGHT_V1_1_PROTOCOL 0
#define LEGACY_BACKLIGHT_PIN 2 // V1.0 fallback only, unverified

enum ElecrowS3BoardVersion { BOARD_VER_UNKNOWN, BOARD_VER_V1_0, BOARD_VER_V1_1_PLUS };
static ElecrowS3BoardVersion boardVersion = BOARD_VER_UNKNOWN;

static bool touchReady = false;

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_rst = -1; // not wired, see the reset dance in _setup_gpio()
    cfg.pin_irq = TOUCH_INT_PIN;
    // rotation:        0      1      2      3
    bool swapXY[4] = {false, true, false, true};
    bool mirrorX[4] = {false, false, true, true};
    bool mirrorY[4] = {false, true, true, false};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

static bool i2cPresent(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static void tca9534Write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(TCA9534_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
    vTaskDelay(pdMS_TO_TICKS(50));

    bool hasExpander = i2cPresent(TCA9534_ADDR);
    bool hasPowerMcu = i2cPresent(BACKLIGHT_I2C_ADDR);
    boardVersion = hasExpander ? BOARD_VER_V1_0 : (hasPowerMcu ? BOARD_VER_V1_1_PLUS : BOARD_VER_UNKNOWN);
    launcherConsolePrintf(
        "Elecrow S3 5in: detected %s\n",
        hasExpander   ? "V1.0 (TCA9534 IO expander)"
        : hasPowerMcu ? "V1.1+ (STC8H1K28 power MCU)"
                      : "unknown board revision (neither 0x18 nor 0x30 answered)"
    );

    // GT911 latches its I2C address (0x5D vs 0x14) from the INT pin level
    // during its own power-on reset, and needs this window to come up
    // reliably at all -- Elecrow's own factory firmware does this on every
    // boot, on every hardware revision.
    pinMode(TOUCH_INT_PIN, OUTPUT);
    digitalWrite(TOUCH_INT_PIN, LOW);
    if (hasExpander) {
        // V1.0 also pulses touch RESET through the expander's pin 2 during
        // this same window.
        tca9534Write(0x03, 0x00);             // all 8 pins as outputs
        tca9534Write(0x01, 0xFF & ~(1 << 1)); // pin 2 LOW
        vTaskDelay(pdMS_TO_TICKS(20));
        tca9534Write(0x01, 0xFF); // pin 2 HIGH
        vTaskDelay(pdMS_TO_TICKS(100));
    } else {
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    pinMode(TOUCH_INT_PIN, INPUT);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    touchReady = hal_touch_init(touchCfg(), TOUCH_ADDR);
    if (!touchReady) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** Backlight has no GPIO on V1.1+ (see the board-revision comment above the
** class); it is set by writing a single byte to the power MCU over I2C. On
** V1.0 it falls back to direct GPIO2 PWM.
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval > 100) brightval = 100;

    if (boardVersion == BOARD_VER_V1_0) {
        static bool attached = false;
        if (!attached) {
            pinMode(LEGACY_BACKLIGHT_PIN, OUTPUT);
            ledcAttach(LEGACY_BACKLIGHT_PIN, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
            attached = true;
        }
        ledcWrite(LEGACY_BACKLIGHT_PIN, ((uint16_t)brightval * 255) / 100);
        return;
    }

#if BACKLIGHT_V1_1_PROTOCOL
    // V1.1: 0x05 (off) .. 0x10 (max), an 11-step range.
    uint8_t val = brightval == 0 ? 0x05 : 0x05 + ((uint16_t)brightval * (0x10 - 0x05)) / 100;
#else
    // V1.2/V1.3: 0 (max) .. 244 (min), 245 (off).
    uint8_t val = brightval == 0 ? 245 : 245 - ((uint16_t)brightval * 245) / 100;
#endif
    Wire.beginTransmission(BACKLIGHT_I2C_ADDR);
    Wire.write(val);
    Wire.endTransmission();
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long d_tmp = launcherMillis();
    if (launcherMillis() - d_tmp > 250 || LongPress) {
        LTouchPoint t;
        if (touchReady && hal_touch_read(touchCfg(), tftWidth, tftHeight, t)) {
            d_tmp = launcherMillis();
            if (!hal_touch_apply(t)) return;
        }
    }
}
