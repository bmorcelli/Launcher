#ifndef LAUNCHER_HAL_POWER_PMIC_H
#define LAUNCHER_HAL_POWER_PMIC_H

#include "../device.h"

bool hal_pmic_init(const DevicePmic &cfg, uint16_t input_current_limit_ma = 3250);

using PmicI2cFptr = int (*)(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len);
bool hal_pmic_init_via_callbacks(
    uint8_t address, PmicI2cFptr readReg, PmicI2cFptr writeReg, uint16_t input_current_limit_ma = 3250
);

void hal_pmic_shutdown();
int hal_pmic_get_input_current_limit_ma();
int hal_pmic_get_charger_constant_curr_ma();
int hal_pmic_get_system_voltage_mv();
int hal_pmic_get_ntc_percent();

#endif
