#include "hal/inputs/buttons.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5IOE1.h>
#include <M5PM1.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <interface.h>

#define UP_BTN 2
#define DW_BTN 3

// --- M5PM1 (power) and M5IOE1 (IO expander) --------------------------------
#define PMIC_SDA 47
#define PMIC_SCL 48
#define IOE1_ADDR 0x4F

#define EPD_ENABLE_PIN M5IOE1_PIN_3   // panel power enable
#define EPD_RESET_PIN M5IOE1_PIN_5    // panel hardware reset
#define TOUCH_POWER_PIN M5IOE1_PIN_13 // touch controller power enable
#define TOUCH_RESET_PIN M5IOE1_PIN_6  // touch controller hardware reset
#define SD_POWER_PIN M5IOE1_PIN_14    // microSD power enable

#define TOUCH_ADDR 0x38
#define TOUCH_IRQ 4

static M5PM1 pm1;
static M5IOE1 ioe1;

static void touchResetCb(bool level) { ioe1.digitalWrite(TOUCH_RESET_PIN, level ? HIGH : LOW); }

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_sda = -1; // Wire1 already begun in _setup_gpio()
    cfg.pin_scl = -1;
    cfg.pin_rst = -1; // reset goes through reset_cb instead
    cfg.pin_irq = TOUCH_IRQ;
    cfg.i2c_bus = &Wire1;
    cfg.reset_cb = touchResetCb;

    // rotation:          0      1      2      3
    bool swapXY[4] = {true, false, true, false};
    bool mirrorX[4] = {false, true, true, false};
    bool mirrorY[4] = {true, true, false, false};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }

    return cfg;
}

static void ioe1SetupOutput(uint8_t pin, uint8_t level) {
    ioe1.pinMode(pin, OUTPUT);
    ioe1.setDriveMode(pin, M5IOE1_DRIVE_PUSHPULL);
    ioe1.digitalWrite(pin, level);
}

static void i2cScanTouchBus() {
    launcherConsolePrintln("I2C1 scan (SDA=47, SCL=48) after touch power/reset:");
    uint8_t found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire1.beginTransmission(addr);
        if (Wire1.endTransmission() == 0) {
            launcherConsolePrintf("  found device @ 0x%02X\n", addr);
            found++;
        }
    }
    if (!found) launcherConsolePrintln("  no I2C devices answered");
}

void _setup_gpio() {
    gpio_hold_dis((gpio_num_t)PMIC_SDA);
    gpio_hold_dis((gpio_num_t)PMIC_SCL);
    gpio_hold_dis((gpio_num_t)TOUCH_IRQ);
    gpio_hold_dis((gpio_num_t)TFT_SCLK);
    gpio_hold_dis((gpio_num_t)TFT_MOSI);
    gpio_hold_dis((gpio_num_t)TFT_CS);
    gpio_hold_dis((gpio_num_t)TFT_DC);
    gpio_hold_dis((gpio_num_t)TFT_BUSY);
    gpio_hold_dis((gpio_num_t)UP_BTN);
    gpio_hold_dis((gpio_num_t)DW_BTN);
    gpio_deep_sleep_hold_dis();

    Wire1.begin(PMIC_SDA, PMIC_SCL);

    if (pm1.begin(&Wire1, M5PM1_DEFAULT_ADDR, PMIC_SDA, PMIC_SCL) != M5PM1_OK) {
        launcherConsolePrintf("%s\n", String("M5PM1 init failed").c_str());
    }

    pm1.setI2cSleepTime(0);
    pm1.wdtSet(0);
    pm1.setChargeEnable(true);
    pm1.setDcdcEnable(true);
    pm1.setLdoEnable(true);
    pm1.setLedEnLevel(false); // status LED off at boot
    pm1.ldoSetPowerHold(true);

    if (ioe1.begin(&Wire1, IOE1_ADDR, PMIC_SDA, PMIC_SCL) != M5IOE1_OK) {
        launcherConsolePrintf("%s\n", String("M5IOE1 init failed").c_str());
    }
    ioe1.setI2cSleepTime(0); // same idle-sleep concern as PM1 above

    ioe1SetupOutput(EPD_ENABLE_PIN, HIGH);
    ioe1SetupOutput(TOUCH_POWER_PIN, HIGH);
    ioe1SetupOutput(SD_POWER_PIN, HIGH);

    ioe1SetupOutput(EPD_RESET_PIN, LOW);
    launcherDelayMs(8);
    ioe1.digitalWrite(EPD_RESET_PIN, HIGH);
    launcherDelayMs(2);

    ioe1SetupOutput(TOUCH_RESET_PIN, HIGH);

    // Frontlight: PM1's own GPIO3/PWM channel 0 (not the M5IOE1 expander).
    // Transcribed from M5GFX's Light_M5PaperMono::init() -- push-pull drive,
    // GPIO repurposed to its "OTHER" (PWM/LED/ADC) function, 5kHz PWM.
    pm1.gpioSetDrive(M5PM1_GPIO_NUM_3, M5PM1_GPIO_DRIVE_PUSHPULL);
    pm1.gpioSetFunc(M5PM1_GPIO_NUM_3, M5PM1_GPIO_FUNC_OTHER);
    pm1.setPwmFrequency(5000);

    i2cScanTouchBus();
    if (!hal_touch_init(touchCfg(), TOUCH_ADDR)) {
        launcherConsolePrintf("%s\n", String("Touch init failed").c_str());
    }

    // UP_BTN -> Prev (single click) / Esc (600ms hold)
    // DW_BTN -> Next (single click) / Sel (600ms hold)
    hal_buttons_init_2(DeviceButtons{DW_BTN, UP_BTN}, 500);
    bright = 50;
}

void _post_setup_gpio() {}

int getBattery() {
    uint16_t mv = 0;
    if (pm1.readVbat(&mv) != M5PM1_OK) return 0;
    int level = (int)(((float)mv - 3300.0f) * 100.0f / (4200.0f - 3300.0f));
    return (level < 0) ? 0 : (level >= 100) ? 100 : level;
}

void _setBrightness(uint8_t brightval) {
    // Same PWM0/GPIO3 recipe as M5GFX's Light_M5PaperMono::setBrightness():
    // duty is brightness^2 (gamma correction) as a 12-bit value; 0 disables
    // the PWM output entirely instead of just driving 0% duty.
    if (brightval == 0) {
        pm1.setPwmDuty12bit(M5PM1_PWM_CH_0, 0, false, false);
        return;
    }
    uint32_t br = static_cast<uint32_t>(brightval) * brightval;
    pm1.setPwmDuty12bit(M5PM1_PWM_CH_0, static_cast<uint16_t>(br >> 4), false, true);
}

void InputHandler(void) {
    static long tm = 0;
    hal_buttons_poll_2();

    if (launcherMillis() - tm > 200 || LongPress) {
        LTouchPoint t;
        if (hal_touch_read(touchCfg(), t)) {
            tm = launcherMillis();
            if (!hal_touch_apply(t)) return;
        }
    }
}

void powerOff() {
    tft->fillScreen(BGCOLOR);
    initDisplay(true);
    tft->setTextSize(FG);
    tft->setTextColor(FGCOLOR);
    tft->drawCentreString("Powered OFF", tftWidth / 2, tftHeight - 100, 1);
    tft->display();
    launcherDelayMs(1000);
    pm1.ldoSetPowerHold(false);
    pm1.shutdown();
    while (1) launcherDelayMs(100);
}
