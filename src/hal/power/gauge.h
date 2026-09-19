#ifndef LAUNCHER_HAL_POWER_GAUGE_H
#define LAUNCHER_HAL_POWER_GAUGE_H

#include "../device.h"

bool hal_gauge_init(const DeviceGauge &cfg);

int hal_gauge_get_percent();

bool hal_gauge_is_charging();

#endif
