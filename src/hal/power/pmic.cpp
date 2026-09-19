#include "pmic.h"

#if defined(PMIC_BQ25896)
#define XPOWERS_CHIP_BQ25896
#include <Wire.h>
#include <XPowersLib.h>

static XPowersPPM ppm;

static void applyOperatingPoint(uint16_t input_current_limit_ma) {
    ppm.setSysPowerDownVoltage(3300);
    ppm.setInputCurrentLimit(input_current_limit_ma);
    ppm.disableCurrentLimitPin();
    ppm.setChargeTargetVoltage(4208);
    ppm.setPrechargeCurr(64);
    ppm.setChargerConstantCurr(832);
    ppm.enableMeasure();
    ppm.disableOTG();
    ppm.enableCharge();
}
#endif

bool hal_pmic_init(const DevicePmic &cfg, uint16_t input_current_limit_ma) {
#if defined(PMIC_BQ25896)
    if (!ppm.init(Wire, cfg.pin_sda, cfg.pin_scl, cfg.address)) return false;
    applyOperatingPoint(input_current_limit_ma);
    return true;
#else
    (void)cfg;
    (void)input_current_limit_ma;
    return false;
#endif
}

bool hal_pmic_init_via_callbacks(
    uint8_t address, PmicI2cFptr readReg, PmicI2cFptr writeReg, uint16_t input_current_limit_ma
) {
#if defined(PMIC_BQ25896)
    if (!ppm.begin(address, readReg, writeReg)) return false;
    applyOperatingPoint(input_current_limit_ma);
    return true;
#else
    (void)address;
    (void)readReg;
    (void)writeReg;
    (void)input_current_limit_ma;
    return false;
#endif
}

void hal_pmic_shutdown() {
#if defined(PMIC_BQ25896)
    ppm.shutdown();
#endif
}

int hal_pmic_get_input_current_limit_ma() {
#if defined(PMIC_BQ25896)
    return (int)ppm.getInputCurrentLimit();
#else
    return -1;
#endif
}

int hal_pmic_get_charger_constant_curr_ma() {
#if defined(PMIC_BQ25896)
    return (int)ppm.getChargerConstantCurr();
#else
    return -1;
#endif
}

int hal_pmic_get_system_voltage_mv() {
#if defined(PMIC_BQ25896)
    return (int)ppm.getSystemVoltage();
#else
    return -1;
#endif
}

int hal_pmic_get_ntc_percent() {
#if defined(PMIC_BQ25896)
    return (int)ppm.getNTCPercentage();
#else
    return -1;
#endif
}
