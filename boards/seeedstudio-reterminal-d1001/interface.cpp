#include "gsl3670_touch.h"
#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SD.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <interface.h>
#include <sd_pwr_ctrl_by_on_chip_ldo.h>

// Seeed reTerminal D1001 -- ESP32-P4 + ESP32-C6 (SDIO WiFi/BT), 800x1280
// portrait JD9365-family DSI panel, GSL3670 I2C touch (board-local driver,
// see gsl3670_touch.*), PCA9535 IO expander for power sequencing, one
// physical button, microSD. Pins/sequencing from Seeed's own ESP-IDF BSP
// (esp32_p4_re_terminal_d1001.h/.c) -- see connections.md for the full
// table and what's still unconfirmed on real hardware. NOT bench-tested.

#define BTN_SEL 3

// I2C bus 1 (BSP_I2C_1): PCA9535 IO expander, RTC, 6DoF, ADC/codec (only the
// expander is used here).
#define IIC_1_SDA 20
#define IIC_1_SCL 21

// PCA9535 IO expander, minimal inline driver -- not present anywhere in
// src/hal, and only a handful of bits are needed here (LCD power/backlight
// enable, battery-read enable, battery-charge enable, power hold), so a
// full expander abstraction would be speculative flexibility this board
// doesn't need (CLAUDE.md). Camera/LTE bits on the same expander are out of
// scope and left untouched (default input).
#define PCA9535_ADDR 0x20 // ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_000
#define PCA9535_REG_OUTPUT0 0x02
#define PCA9535_REG_OUTPUT1 0x03
#define PCA9535_REG_CONFIG0 0x06
#define PCA9535_REG_CONFIG1 0x07

// Port0 bits (BPS_LCD_PWR_EN=0, BSP_LCD_BACKLIGHT_EN=7, BSP_BAT_READ_EN=6).
#define PCA_LCD_PWR_EN (1 << 0)
#define PCA_LCD_BACKLIGHT_EN (1 << 7)
#define PCA_BAT_READ_EN (1 << 6)
// Port1 bits, local numbering (global bit - 8): BSP_PWR_HOLD=8 -> bit0,
// BSP_BAT_CHARGE_EN=10 -> bit2 (active LOW: 0 enables charging), touch RST
// (BSP_LCD_TOUCH_RST)=12 -> bit4. NOT GPIO12 on the bare P4 -- that pin is
// BSP_WIFI_BOOT (the C6 co-processor's boot strap), a completely different,
// unrelated signal. Confirmed on real hardware that driving raw GPIO12 as
// "touch reset" never actually reset the touch chip (see gsl3670_touch.h).
#define PCA_PWR_HOLD (1 << 0)
#define PCA_BAT_CHARGE_EN (1 << 2)
#define PCA_TOUCH_RST (1 << 4)

// microSD (SDMMC slot 0, IOMUX pins -- see PIN_SD_* build flags). Power rail
// is a discrete GPIO on this board (BSP_SD_PWR_EN), separate from the P4's
// on-chip SDMMC IO-domain LDO (BOARD_SDMMC_POWER_CHANNEL, powered by
// SD_MMC.begin() itself). Polarity confirmed active-HIGH against the BSP.
#define SD_PWR_EN 46
#define SD_DETECT 45 // input, not currently polled -- see connections.md

static bool _pcaWriteReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(PCA9535_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

// Cached PORT1 output byte (PWR_HOLD / BAT_CHARGE_EN / touch RST). PCA9535
// output registers have to be written whole-byte, and touch reset is now
// toggled from a different call site (gsl3670_touch.cpp, via
// d1001TouchResetSet() below) than the power-up sequence -- without this
// cache, toggling one bit would clobber whatever the other bits were last
// set to.
static uint8_t _pcaOutput1 = 0;

// Powers up the board (BSP_PWR_HOLD must be held HIGH or the board loses its
// own supply -- confirmed against bsp_power_init(), not a "nice to have"),
// then brings up the LCD/touch power/backlight rail and gates the
// battery-read divider on. Touch RST starts deasserted (HIGH, active LOW)
// here too, alongside PWR_HOLD -- gsl3670_init() drives its own reset pulses
// later through d1001TouchResetSet().
//
// Staggered in two steps, matching bsp_power_init() exactly (PWR_HOLD -> 50ms
// settle -> everything else -> 50ms settle) rather than one combined write.
static bool _pcaPowerUp() {
    bool ok = true;
    _pcaOutput1 = PCA_PWR_HOLD | PCA_TOUCH_RST; // BAT_CHARGE_EN left 0 -> charging enabled
    ok &= _pcaWriteReg(PCA9535_REG_OUTPUT1, _pcaOutput1);
    ok &= _pcaWriteReg(PCA9535_REG_CONFIG1, (uint8_t)~(PCA_PWR_HOLD | PCA_BAT_CHARGE_EN | PCA_TOUCH_RST));
    launcherDelayMs(50);

    ok &= _pcaWriteReg(PCA9535_REG_OUTPUT0, PCA_LCD_PWR_EN | PCA_LCD_BACKLIGHT_EN | PCA_BAT_READ_EN);
    ok &= _pcaWriteReg(
        PCA9535_REG_CONFIG0, (uint8_t)~(PCA_LCD_PWR_EN | PCA_LCD_BACKLIGHT_EN | PCA_BAT_READ_EN)
    );
    launcherDelayMs(50);
    return ok;
}

static void _pcaPowerHoldRelease() {
    // Only PWR_HOLD needs clearing to drop the board's own supply -- leaving
    // the direction register alone is fine, the pin is already an output.
    _pcaOutput1 = 0;
    _pcaWriteReg(PCA9535_REG_OUTPUT1, _pcaOutput1);
}

// Touch reset callback handed to gsl3670_init() (see gsl3670_touch.h) --
// PCA9535 expander bit 12 (BSP_LCD_TOUCH_RST), active LOW, on I2C bus 1
// (plain Wire, same bus/object as the rest of this file's PCA9535 access).
// NOT a raw GPIO -- see the PCA_TOUCH_RST comment above for why that was the
// actual bug behind touch never surviving a cold boot.
static void d1001TouchResetSet(bool level) {
    if (level) _pcaOutput1 |= PCA_TOUCH_RST;
    else _pcaOutput1 &= (uint8_t)~PCA_TOUCH_RST;
    _pcaWriteReg(PCA9535_REG_OUTPUT1, _pcaOutput1);
}

// BSP_BUTTON_IN is wired active-HIGH with its own external pull-down --
// confirmed on real hardware (button read as permanently "pressed" until
// this was set): no internal pull-up, and the read polarity flips via
// DeviceButtons::activeHigh (src/hal/inputs/buttons.cpp).
static DeviceButtons buttonsCfg() {
    DeviceButtons cfg;
    cfg.btn1 = BTN_SEL;
    cfg.pullup = false;
    cfg.activeHigh = true;
    return cfg;
}

static bool touchReady = false;

// The SDMMC-slot-0 pins sit in an IO domain fed by an on-chip LDO on the P4
// (SOC_SDMMC_IO_POWER_EXTERNAL). SD_MMC.begin() would power it via
// BOARD_SDMMC_POWER_CHANNEL automatically, but this board's SD card is
// wired on plain SPI instead (see platformio.ini -- ESP-Hosted can't run
// SDIO1/SD and SDIO2/WiFi concurrently on the P4), and the SD/SPIClass path
// knows nothing about that LDO, so it has to be powered by hand once, same
// as lilygo-t-display-p4's SPI-mode SD path does.
static void _powerSdCardIoLdo() {
    sd_pwr_ctrl_ldo_config_t ldoConfig = {.ldo_chan_id = BOARD_SDMMC_POWER_CHANNEL};
    sd_pwr_ctrl_handle_t handle = nullptr;
    if (sd_pwr_ctrl_new_on_chip_ldo(&ldoConfig, &handle) != ESP_OK) {
        launcherConsolePrintln("SD card IO LDO: failed to acquire channel");
        return;
    }
    if (sd_pwr_ctrl_set_io_voltage(handle, 3300) != ESP_OK) {
        launcherConsolePrintln("SD card IO LDO: failed to set 3.3V");
    }
}

// Power-cycles the board's own discrete SD rail switch (BSP_SD_PWR_EN),
// same two-step sequence bsp_power_init() uses before mounting. Polarity
// confirmed active-HIGH against the reference driver
// (gpio_set_level(BSP_SD_PWR_EN, 0) then 1 immediately before
// bsp_sdcard_mount()).
static void _powerCycleSdRail() {
    launcherGpioOutput(SD_PWR_EN);
    launcherGpioWrite(SD_PWR_EN, LOW);
    launcherDelayMs(100);
    launcherGpioWrite(SD_PWR_EN, HIGH);
}

void _setup_gpio() {
    launcherGpioInputPullup(SD_DETECT);

    Wire.begin(IIC_1_SDA, IIC_1_SCL);
    if (!_pcaPowerUp()) { launcherConsolePrintln("PCA9535 IO expander not found -- board may lose power"); }
    launcherDelayMs(50);

    _powerSdCardIoLdo();
    _powerCycleSdRail();

    launcherDelayMs(250);

    hal_buttons_init(buttonsCfg(), 1);

    launcherWifiInitHostedSdioGuarded(
        SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST
    );
}

void _post_setup_gpio() {
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);
}

void _late_setup_gpio() {
    touchReady = gsl3670_init(GSL3670_SDA, GSL3670_SCL, d1001TouchResetSet);
    launcherConsolePrintf("GSL3670 touch: %s\n", touchReady ? "started" : "not found");
}

// getBattery() is not overridden -- the weak default in mykeyboard.cpp reads
// ANALOG_BAT_PIN (GPIO18). BSP_BAT_READ_EN is gated on once in _setup_gpio()
// (PCA9535) and left on for the session. The divider ratio/multiplier is
// unconfirmed on hardware -- see connections.md.

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

void InputHandler(void) {
    hal_buttons_poll_1(buttonsCfg());
    if (AnyKeyPress) return;

    if (!touchReady) return;
    static long lastRead = launcherMillis();
    Gsl3670Point p;
    uint8_t n = gsl3670_read(&p, 1);

    if (launcherMillis() - lastRead < 200 && !LongPress) return;
    lastRead = launcherMillis();

    if (n == 0) return;

    if (wakeUpScreen()) return;
    AnyKeyPress = true;

    // Native panel 800(w)x1280(h) -> current screen rotation. Same
    // per-rotation table lilygo-t-display-p4 uses for its own portrait-native
    // DSI panel. Confirmed on real hardware for rotation 1 (build default,
    // perfectly aligned) and rotation 3 (both axes come out mirrored versus
    // rotation 1, exactly as this table predicts -- rotation 3 is rotation 1
    // rotated 180 deg.). Rotations 0/2 are not independently confirmed on
    // this panel, but follow from the same consistent 4-way rotation table,
    // not a separate guess.
    constexpr uint16_t panelW = 800;
    constexpr uint16_t panelH = 1280;
    const uint16_t nx = p.x; // native, 0..panelW-1
    const uint16_t ny = p.y; // native, 0..panelH-1
    uint16_t sx, sy;
    switch (rotation) {
        case 1:
            sx = ny;
            sy = (panelW - 1) - nx;
            break;
        case 2:
            sx = (panelW - 1) - nx;
            sy = (panelH - 1) - ny;
            break;
        case 3:
            sx = (panelH - 1) - ny;
            sy = nx;
            break;
        default: // rotation 0 - native orientation
            sx = nx;
            sy = ny;
            break;
    }
    touchPoint.x = sx;
    touchPoint.y = sy;
    touchPoint.pressed = true;
    touchHeatMap(touchPoint);
}

// Cuts the SD card's power rail before a reset/power-off, not just stops
// using it -- yanking the CPU mid-transaction leaves the card powered and in
// an undefined state, and neither a reset nor deep sleep power-cycles it on
// their own, so it can come back up refusing to initialize for whatever
// firmware boots next (same rationale/pattern as
// lilygo-t-display-p4's _peripherals_power_down()).
static void _peripheralsPowerDown() {
    SD.end();
    _setBrightness(0);
    launcherGpioWrite(SD_PWR_EN, LOW); // active-HIGH -- LOW cuts power
}

void powerOff() {
    _peripheralsPowerDown();
    launcherDelayMs(100);
    // Dropping PWR_HOLD cuts the board's own 3V3 rail -- this is how the BSP
    // itself powers the board off (bsp_power_off()). If the expander is
    // unreachable this falls through to deep sleep instead. The P4 has no
    // ext0 wakeup source (RTC IO wakeup is ext1-only on this chip), so no
    // wakeup source is armed here -- PWR_HOLD dropping is expected to cut
    // power outright.
    _pcaPowerHoldRelease();
    launcherDelayMs(200);
    esp_deep_sleep_enable_gpio_wakeup((gpio_num_t)BTN_SEL, ESP_GPIO_WAKEUP_GPIO_HIGH);
    esp_deep_sleep_start();
}

void reboot() {
    // Power-cycle the SD card's rail before the CPU resets -- see
    // _peripheralsPowerDown().
    _peripheralsPowerDown();
    launcherDelayMs(200);
    ESP.restart();
}
