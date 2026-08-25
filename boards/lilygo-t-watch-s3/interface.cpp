#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SPI.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include <interface.h>

XPowersAXP2101 axp192;

// Haptic
#include "HapticDrivers.hpp"
HapticDriver_DRV2605 drv;

// Touch is on Wire1 (Wire is the sensor/PMIC bus) -- see cfg.i2c_bus below.
static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.i2c_bus = &Wire1;
    // rotation:        0      1      2      3
    bool swapXY[4] = {false, true, false, true};
    bool mirrorX[4] = {true, true, false, false};
    bool mirrorY[4] = {true, false, false, true};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

void _setup_gpio() {
    launcherGpioInput(16); // Touch IRQ
    Wire.begin(10, 11);    // sensors
    launcherDelayMs(10);
    Wire1.begin(39, 40); // touchscreen
    launcherDelayMs(10);
    axp192.init(Wire, 10, 11);
    axp192.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
    axp192.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_900MA);
    axp192.setSysPowerDownVoltage(2600);
    axp192.setALDO1Voltage(3300);
    axp192.setALDO2Voltage(3300);
    axp192.setALDO3Voltage(3300);
    axp192.setALDO4Voltage(3300);
    axp192.setBLDO2Voltage(3300);
    axp192.setDC3Voltage(3300);
    axp192.enableDC3(); // gps
    axp192.disableDC2();
    axp192.disableDC4();
    axp192.disableDC5();
    axp192.disableBLDO1();
    axp192.disableCPUSLDO();
    axp192.disableDLDO1();
    axp192.disableDLDO2();
    axp192.enableALDO1(); //! RTC VBAT
    axp192.enableALDO2(); //! TFT BACKLIGHT   VDD
    axp192.enableALDO3(); //! Screen touch VDD
    axp192.enableALDO4(); //! Radio VDD
    axp192.enableBLDO2(); //! drv2605 enable
    //  Set the time of pressing the button to turn off
    axp192.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    // Set the button power-on press time
    axp192.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
    // It is necessary to disable the detection function of the TS pin on the board
    // without the battery temperature detection function, otherwise it will cause abnormal charging
    axp192.disableTSPinMeasure();
    // Enable internal ADC detection
    axp192.enableBattDetection();
    axp192.enableVbusVoltageMeasure();
    axp192.enableBattVoltageMeasure();
    axp192.enableSystemVoltageMeasure();
    // t-watch no chg led
    axp192.setChargingLedMode(XPOWERS_CHG_LED_OFF);
    axp192.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    // Enable the required interrupt function
    axp192.enableIRQ(
        XPOWERS_AXP2101_BAT_INSERT_IRQ | XPOWERS_AXP2101_BAT_REMOVE_IRQ |    // BATTERY
        XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_VBUS_REMOVE_IRQ |  // VBUS
        XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ |     // POWER KEY
        XPOWERS_AXP2101_BAT_CHG_DONE_IRQ | XPOWERS_AXP2101_BAT_CHG_START_IRQ // CHARGE
    );

    // Clear all interrupt flags
    axp192.clearIrqStatus();
    // Set the precharge charging current
    axp192.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
    // Set constant current charge current limit
    axp192.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_300MA);
    // Set stop charging termination current
    axp192.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
    // Set charge cut-off voltage
    axp192.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V35);
    // Set RTC Battery voltage to 3.3V
    axp192.setButtonBatteryChargeVoltage(3300);
    axp192.enableButtonBatteryCharge();

    hal_touch_init(touchCfg());

    // Haptic driver
    if (!drv.begin(Wire, 10, 11)) {
        launcherConsolePrintf("%s\n", String("Failed to find DRV2605.").c_str());
    } else {
        launcherConsolePrintf("%s\n", String("Init DRV2605 Sensor success!").c_str());
        drv.selectLibrary(1);
        drv.setMode(HapticMode::INTERNAL_TRIGGER);
        drv.setERMLRA(true);

        // Startup buzz
        drv.setWaveform(0, 70);
        drv.setWaveform(1, 0);
        drv.run();
    }
}

void _post_setup_gpio() {
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);
}

int getBattery() {
    int percent = axp192.getBatteryPercent();
    return percent;
}

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

bool getTouched() { return launcherGpioRead(16) == LOW; }

void InputHandler(void) {
    static unsigned long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        // I know R3CK.. I Should NOT nest if statements..
        // but it is needed to not keep SPI bus used without need, it save resources
        if (getTouched()) {
            LTouchPoint t;
            if (hal_touch_read(touchCfg(), t)) {
                tm = launcherMillis();
                if (!hal_touch_apply(t)) return;
                drv.setWaveform(0, 75);
                drv.setWaveform(1, 0); // end waveform
                drv.run();
            }
        }
    }
}

void powerOff() {
    axp192.disableALDO2(); //! TFT BACKLIGHT   VDD
    axp192.disableALDO3(); //! Screen touch VDD
    axp192.disableALDO4(); //! Radio VDD
    axp192.disableBLDO2(); //! drv2605 enable
    axp192.shutdown();
}

bool isCharging() {
    return axp192.isCharging(); // Return the charging status from AXP
}

void reboot() {
    launcherConsoleFlush();

    ledcWrite(TFT_BL, 0);
    launcherGpioWrite(TFT_BL, LOW);

    drv.setWaveform(0, 0);
    drv.setWaveform(1, 0);

    axp192.enableALDO1(); //! RTC VBAT
    axp192.enableALDO2(); //! TFT BACKLIGHT   VDD

    // Force the touch controller to lose power so the next boot starts from a clean state.
    axp192.disableALDO3();
    axp192.disableBLDO2();
    launcherDelayMs(50);
    axp192.enableALDO3(); //! Screen touch VDD
    axp192.enableBLDO2(); //! drv2605 enable

    launcherDelayMs(100);
    esp_restart();
}
