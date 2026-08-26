#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

// GPIO expander
#include <ExtensionIOXL9555.hpp>
ExtensionIOXL9555 io;
// Interrupt IO port
#define TP_INT (12)
#define RTC_INT (1)
#define PMU_INT (7)
#define NFC_INT (5)
#define SENSOR_INT (8)
#define NFC_CS (4)

// External expansion chip IO definition
#define EXPANDS_DRV_EN (6)
#define EXPANDS_DISP_EN (7)
#define EXPANDS_TOUCH_RST (8)
#define EXPANDS_SD_DET (10)

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
XPowersAXP2101 PPM;

#include "hal/device.h"
#include "hal/inputs/touch.h"

static bool touch_OK = false;

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    // Reset is wired through the IO expander (EXPANDS_TOUCH_RST), not a
    // direct GPIO -- pulsed by hand in _setup_gpio() before hal_touch_init(),
    // same as lilygo-t-deck-pro's MAX variant.
    cfg.pin_rst = -1;
    cfg.pin_irq = TP_INT;
    // Derived from the manual per-rotation math the original InputHandler()
    // did on the raw point (rot0: swap only; rot1: mirrorY only, against
    // TFT_WIDTH -- which is displayConfig.width, i.e. hal_touch's screenH at
    // odd rotations; rot2: swap+mirrorX+mirrorY; rot3: identity).
    // rotation:        0     1      2     3
    bool swapXY[4] = {true, false, true, false};
    bool mirrorX[4] = {false, false, true, false};
    bool mirrorY[4] = {false, true, true, false};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

void _setup_gpio() {
    launcherConsoleBegin(115200);
    uint8_t csPin[4] = {4, 21, 36, 41}; // NFC,SDCard, LoRa, TFT
    for (auto pin : csPin) {
        launcherGpioOutput(pin);
        launcherGpioWrite(pin, HIGH);
    }
    Wire.begin(SDA, SCL);
    bool pmu_ret = false;
    pmu_ret = PPM.init(Wire, SDA, SCL, AXP2101_SLAVE_ADDRESS);
    if (pmu_ret) {
        PPM.setSysPowerDownVoltage(3300);
        PPM.setChargeTargetVoltage(4208);
        PPM.setChargerConstantCurr(832);
        PPM.getChargerConstantCurr();
        PPM.setALDO1Voltage(3300); // SD Card
        PPM.enableALDO1();
        PPM.setALDO2Voltage(3300); // Display
        PPM.enableALDO2();
        PPM.setALDO4Voltage(3300); // Sensor
        PPM.enableALDO4();

        launcherConsolePrintf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
    }
    if (io.begin(Wire, 0x20)) {
        const uint8_t expands[] = {
            EXPANDS_DISP_EN,
            EXPANDS_DRV_EN,
            EXPANDS_TOUCH_RST,
            EXPANDS_SD_DET,
        };
        for (auto pin : expands) {
            io.pinMode(pin, OUTPUT);
            io.digitalWrite(pin, HIGH);
            launcherDelayMs(1);
        }
    } else {
        launcherConsolePrintf("%s\n", String("Initializing expander failed").c_str());
    }
    io.digitalWrite(EXPANDS_TOUCH_RST, LOW);
    launcherDelayMs(20);
    io.digitalWrite(EXPANDS_TOUCH_RST, HIGH);
    launcherDelayMs(60);
    touch_OK = hal_touch_init(touchCfg(), 0x1A);
    if (!touch_OK) { launcherConsolePrintf("%s\n", String("touch is not online...").c_str()); }
}

int getBattery() {
    int percent = 0;
    percent = PPM.getBatteryPercent();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

void _setBrightness(uint8_t brightval) {
    // The AMOLED has no backlight rail; brightness is a CO5300 register.
    // outputDriver() is the panel behind the canvas — see DisplayDrivers.
    auto *panel = static_cast<Arduino_CO5300 *>(tft->outputDriver());
    if (panel) panel->setBrightness(brightval * 254 / 100);
}

void InputHandler(void) {
    if (!touch_OK) return; // dont have touchscreen
    static long tm = 0;
    if (launcherMillis() - tm < 200 && !LongPress) return;
    LTouchPoint t;
    if (hal_touch_read(touchCfg(), t)) {
        tm = launcherMillis();
        hal_touch_apply(t);
    }
}

void powerOff() {
    const uint8_t expands[] = {
        EXPANDS_DISP_EN,
        EXPANDS_DRV_EN,
        EXPANDS_TOUCH_RST,
        EXPANDS_SD_DET,
    };
    for (auto pin : expands) {
        io.digitalWrite(pin, LOW);
        launcherDelayMs(1);
    }
    PPM.shutdown();

    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
