#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

// ─── Waveshare ESP32-C5-Touch-LCD-3.5 ────────────────────────────────────────
// ESP32-C5 + ST7796 320x480 + FT6336 capacitive touch. Hardware twin of the
// Pancake C5, with one structural difference: the panel reset and backlight are
// NOT GPIOs. A CH32V006 I/O expander at 0x24 on the shared I2C bus owns panel
// power, the LCD reset, the FT6336 reset and the backlight PWM. Touch, expander
// and PMU all live on the one shared bus.
//
// Pin map and bring-up order are from the ESP32-C5-BSP (hardware-verified):
//   I2C:  SDA = GPIO27  SCL = GPIO26         (shared: expander, touch, PMU)
//   TFT:  MOSI 7  MISO 2  SCLK 6  CS 8  DC 5   (reset = EXIO1, no GPIO)
//   TP:   FT6336 @ 0x38, INT = GPIO3          (reset = EXIO0, no GPIO)
//   SD:   CS = GPIO9 on the TFT SPI bus
//   PMU:  AXP2101 @ 0x34

// ---- shared I2C ----
#define WS_I2C_SDA 27
#define WS_I2C_SCL 26
#define WS_I2C_HZ 100000

// ---- FT6336 capacitive touch (shared bus; reset handled by the expander) ----
#define TOUCH_SDA WS_I2C_SDA
#define TOUCH_SCL WS_I2C_SCL
#define TOUCH_RST -1 // FT6336 reset is EXIO0 on the 0x24 expander
#define TOUCH_INT 3  // TP_INT = GPIO3 (BSP INT gate)

// ---- CH32V006 I/O expander (0x24): panel power, resets, backlight PWM --------
#define EXIO_ADDR 0x24
#define EXIO_MODE_REG 0x02
#define EXIO_OUT_REG 0x03
#define EXIO_PWM_REG 0x05
#define EXIO_TP_RST 0  // EXIO0 -> FT6336 reset
#define EXIO_LCD_RST 1 // EXIO1 -> ST7796 reset
#define EXIO_POWER 5   // EXIO5 -> panel power / backlight rail

static uint8_t s_exioOut = 0;
static bool s_exioReady = false;

static bool exioWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(EXIO_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

// Bring the panel rail up with both resets released, then pulse each reset
// (active-low) with the rail up -- resetting the FT6336 while its rail is off
// leaves it unresponsive. Run once: pulsing EXIO1 again after the ST7796 is
// initialised would blank it. Sequence verified on hardware in the ESP32-C5-BSP.
static void expanderInit() {
    if (s_exioReady) return;
    Wire.begin(WS_I2C_SDA, WS_I2C_SCL, WS_I2C_HZ);

    const uint8_t ctrl = (1U << EXIO_TP_RST) | (1U << EXIO_LCD_RST) | (1U << EXIO_POWER);
    exioWrite(EXIO_MODE_REG, 0xFF);
    exioWrite(EXIO_OUT_REG, 0x00);
    exioWrite(EXIO_PWM_REG, 0x00);
    exioWrite(EXIO_MODE_REG, ctrl);

    s_exioOut = (1U << EXIO_TP_RST) | (1U << EXIO_LCD_RST);
    exioWrite(EXIO_OUT_REG, s_exioOut);
    delay(50);
    s_exioOut = 0x00;
    exioWrite(EXIO_OUT_REG, s_exioOut);
    delay(50);
    s_exioOut = ctrl;
    exioWrite(EXIO_OUT_REG, s_exioOut);
    delay(50);

    // power is on; pulse each reset (active-low) with the rail up
    s_exioOut &= ~(1U << EXIO_LCD_RST);
    exioWrite(EXIO_OUT_REG, s_exioOut);
    delay(20);
    s_exioOut |= (1U << EXIO_LCD_RST);
    exioWrite(EXIO_OUT_REG, s_exioOut);
    delay(120);
    s_exioOut &= ~(1U << EXIO_TP_RST);
    exioWrite(EXIO_OUT_REG, s_exioOut);
    delay(20);
    s_exioOut |= (1U << EXIO_TP_RST);
    exioWrite(EXIO_OUT_REG, s_exioOut);
    delay(300); // FT6336 boot

    s_exioReady = true;
}

// Backlight duty 0..255 via the expander PWM register (there is no BL GPIO).
static void expanderBacklight(uint8_t duty) {
    if (!s_exioReady) return;
    exioWrite(EXIO_PWM_REG, duty);
}

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_sda = TOUCH_SDA;
    cfg.pin_scl = TOUCH_SCL;
    cfg.pin_rst = TOUCH_RST;
    cfg.pin_irq = TOUCH_INT;
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

// ─── AXP2101 PMU (0x34 on the shared bus) ─────────────────────────────────────
// Raw I2C reads (like the Pancake's MAX17048 path) to avoid pulling XPowersLib
// onto the C5. Register map matches the AXP2101; values want on-hardware
// validation against the BSP reference (src/board/ws_c5_35/ws_c5_35_battery.*).
#define AXP2101_ADDR 0x34
#define AXP2101_REG_BAT_PERCENT 0xA4 // battery SOC, 0..100 (fuel gauge)
#define AXP2101_REG_STATUS2 0x01     // charge status in bits [6:5]

int getBattery() {
    Wire.beginTransmission(AXP2101_ADDR);
    Wire.write(AXP2101_REG_BAT_PERCENT);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom((int)AXP2101_ADDR, 1) != 1) return 0;
    int pct = Wire.read();
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    return pct;
}

bool isCharging() {
    Wire.beginTransmission(AXP2101_ADDR);
    Wire.write(AXP2101_REG_STATUS2);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)AXP2101_ADDR, 1) != 1) return false;
    uint8_t st = Wire.read();
    return ((st >> 5) & 0x03) == 0x01; // 01 = charging
}

// ─── Launcher board hooks ─────────────────────────────────────────────────────

void _setup_gpio() {
    // Keep SD and TFT off the shared SPI bus during bring-up.
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);

    // Power the panel rail and release the LCD/TP resets BEFORE tft.init():
    // both reset and backlight live on the expander, not on GPIOs.
    expanderInit();
}

void _post_setup_gpio() {
    // Backlight up after the panel is initialised (expander PWM, not hal_bright).
    expanderBacklight((uint8_t)bright);

    // FT6336 capacitive touch on the shared bus. Raise the threshold (chip
    // default ~22) to cut phantom touches; the BSP also gates reads on INT=GPIO3.
    hal_touch_init(touchCfg());
    hal_touch_set_threshold(40);
}

void _setBrightness(uint8_t brightval) { expanderBacklight(brightval); }

void InputHandler(void) {
    static long tm = launcherMillis();
    if (launcherMillis() - tm > 250 || LongPress) {
        LTouchPoint t;
        checkPowerSaveTime();
        if (hal_touch_read(touchCfg(), t)) {
            tm = launcherMillis();
            if (!hal_touch_apply(t)) return;
        }
    }
}

void powerOff() { esp_deep_sleep_start(); }
