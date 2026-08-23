#ifndef LAUNCHER_HAL_POWER_GAUGE_H
#define LAUNCHER_HAL_POWER_GAUGE_H

#include "../device.h"

// Fuel gauge HAL. Backend selected by a board build_flags macro:
// GAUGE_BQ27220 (implemented), GAUGE_MAX17048 (not yet migrated -- see
// docs/plan.md). With no macro defined, every call is a no-op / sentinel,
// so boards without a gauge pay nothing.

// Sets the design capacity if it doesn't already match. Wire must already
// be begun by the caller (the gauge driver talks to the default Wire bus).
bool hal_gauge_init(const DeviceGauge &cfg);

// 0-100, or -1 if the gauge isn't ready / not present.
int hal_gauge_get_percent();

bool hal_gauge_is_charging();

#endif
