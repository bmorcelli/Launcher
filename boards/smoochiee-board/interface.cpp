#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "hal/power/pmic.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"

#define SEL_BTN 0
#define UP_BTN 41
#define DW_BTN 40
#define R_BTN 38
#define L_BTN 39
#define BTN_ACT LOW
#define CC1101_SS_PIN 46
#define NRF24_SS_PIN 14
#define GROVE_SDA 47
#define GROVE_SCL 48
#define SMOOCHIEE_BQ25896_ADDRESS 0x6B

// Power handler for battery detection -- no separate fuel gauge on this
// board, battery% comes from the PMIC's own system voltage reading.
#include <Wire.h>

static DeviceButtons buttonsCfg() { return DeviceButtons{L_BTN, R_BTN, UP_BTN, DW_BTN, SEL_BTN}; }

void _setup_gpio() {

    hal_buttons_init(buttonsCfg(), 5);

    launcherGpioOutput(CC1101_SS_PIN);
    launcherGpioOutput(NRF24_SS_PIN);
    launcherGpioOutput(45);

    launcherGpioWrite(45, HIGH);
    launcherGpioWrite(CC1101_SS_PIN, HIGH);
    launcherGpioWrite(NRF24_SS_PIN, HIGH);
    // Starts SPI instance for CC1101 and NRF24 with CS pins blocking communication at start

    Wire.begin(GROVE_SDA, GROVE_SCL);
    DevicePmic pmicCfg{GROVE_SDA, GROVE_SCL, SMOOCHIEE_BQ25896_ADDRESS};
    if (!hal_pmic_init(pmicCfg)) { launcherConsolePrintln("PMIC: Failed starting BQ25896"); }
}

int getBattery() {
    uint8_t percent = (hal_pmic_get_system_voltage_mv() - 3300) * 100 / (float)(4150 - 3350);
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

void InputHandler(void) { hal_buttons_poll_5(buttonsCfg()); }
