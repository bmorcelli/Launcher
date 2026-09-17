#include "display/Arduino_CO5300.h"
#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "hal/inputs/touch.h"

#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <XPowersLib.h>
#include <interface.h>

#define BOARD_I2C_SDA 15
#define BOARD_I2C_SCL 14
#define BOARD_TOUCH_INT 38
#define BOARD_TOUCH_RST 9
#define BOARD_TOUCH_ADDRESS 0x38
#define BTN1 0
#define BTN2 10

XPowersAXP2101 axp2101;

static bool touch_OK = false;

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_rst = BOARD_TOUCH_RST;
    cfg.pin_irq = BOARD_TOUCH_INT;
    return cfg;
}

void _setup_gpio() {
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    axp2101.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL);

    axp2101.clearIrqStatus();

    axp2101.enableVbusVoltageMeasure();
    axp2101.enableBattVoltageMeasure();
    axp2101.enableSystemVoltageMeasure();
    axp2101.enableTemperatureMeasure();

    // It is necessary to disable the detection function of the TS pin on the board
    // without the battery temperature detection function, otherwise it will cause abnormal charging
    axp2101.disableTSPinMeasure();

    // Disable all interrupts
    axp2101.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    // Clear all interrupt flags
    axp2101.clearIrqStatus();
    // Enable the required interrupt function
    axp2101.enableIRQ(
        XPOWERS_AXP2101_BAT_INSERT_IRQ | XPOWERS_AXP2101_BAT_REMOVE_IRQ |    // BATTERY
        XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_VBUS_REMOVE_IRQ |  // VBUS
        XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ |     // POWER KEY
        XPOWERS_AXP2101_BAT_CHG_DONE_IRQ | XPOWERS_AXP2101_BAT_CHG_START_IRQ // CHARGE
        // XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ | XPOWERS_AXP2101_PKEY_POSITIVE_IRQ   |   //POWER KEY
    );

    hal_buttons_init_2(DeviceButtons{BTN1, BTN2}, 600);

    touch_OK = hal_touch_init(touchCfg(), BOARD_TOUCH_ADDRESS);
}

int getBattery() {
    int percent = axp2101.getBatteryPercent();
    return percent;
}

void _setBrightness(uint8_t brightval) {
    auto *panel = static_cast<Arduino_CO5300 *>(tft->outputDriver());
    if (panel) panel->setBrightness((brightval * 255) / 100);
}


void InputHandler(void) {
    if (touch_OK) {
        static unsigned long tm = 0;
        LTouchPoint t;
        bool touched = hal_touch_read(touchCfg(), t);
        vTaskDelay(pdMS_TO_TICKS(50));
        if ((launcherMillis() - tm) > 200 || LongPress) { // one reading each 500ms
            if (touched) {
                tm = launcherMillis();
                if (!hal_touch_apply(t)) return;
            }
        }
    }

    hal_buttons_poll_2();
    return;
}

void powerOff() { axp2101.shutdown(); }

void reboot() {
    ESP.restart();
}
