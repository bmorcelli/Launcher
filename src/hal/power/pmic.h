#ifndef LAUNCHER_HAL_POWER_PMIC_H
#define LAUNCHER_HAL_POWER_PMIC_H

#include "../device.h"

// Charger IC (PMIC) HAL. Backend selected by a board build_flags macro:
// PMIC_BQ25896 (implemented), PMIC_AXP2101/PMIC_AXP192/PMIC_SY6970 (not
// yet migrated -- see docs/plan.md). With no macro defined, every call is
// a no-op that returns false, so boards without a PMIC pay nothing.

// Initializes the charger with the same fixed operating point every
// BQ25896 board in this project uses today (3300mV power-down, 4208mV
// charge target, 64mA precharge, 832mA constant charge current). Input
// current limit defaults to 3250mA (what most boards use) but can be
// overridden -- e.g. reaper uses 2000mA. Wire must already be begun by
// the caller.
bool hal_pmic_init(const DevicePmic &cfg, uint16_t input_current_limit_ma = 3250);

// Same operating point as hal_pmic_init(), but talks to the charger through
// caller-provided register read/write callbacks instead of a raw Wire bus
// -- for boards that must reuse an I2C bus another driver already began
// (e.g. lilygo-t5-epaper-s3-pro, sharing the EPD_Painter bus).
using PmicI2cFptr = int (*)(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len);
bool hal_pmic_init_via_callbacks(
    uint8_t address, PmicI2cFptr readReg, PmicI2cFptr writeReg, uint16_t input_current_limit_ma = 3250
);

void hal_pmic_shutdown();

// Diagnostics -- mirrors the getInputCurrentLimit()/getChargerConstantCurr()
// serial logging every BQ25896 board printed after init.
int hal_pmic_get_input_current_limit_ma();
int hal_pmic_get_charger_constant_curr_ma();

// For boards with no separate fuel gauge, estimating battery% from the
// PMIC's own system voltage reading (e.g. smoochiee-board).
int hal_pmic_get_system_voltage_mv();

#endif
