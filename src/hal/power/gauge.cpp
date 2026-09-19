#include "gauge.h"

#if defined(GAUGE_BQ27220)
#include <bq27220.h>

static BQ27220 gauge;
#endif

bool hal_gauge_init(const DeviceGauge &cfg) {
#if defined(GAUGE_BQ27220)
    if (cfg.design_capacity_mah == 0) return true; // don't do anything
    if (gauge.getDesignCap() != cfg.design_capacity_mah) gauge.setDesignCap(cfg.design_capacity_mah);
    return true;
#else
    (void)cfg;
    return false;
#endif
}

int hal_gauge_get_percent() {
#if defined(GAUGE_BQ27220)
    int percent = gauge.getChargePcnt();
    if (percent == 65535) return -1;
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
#else
    return -1;
#endif
}

bool hal_gauge_is_charging() {
#if defined(GAUGE_BQ27220)
    return gauge.getIsCharging();
#else
    return false;
#endif
}
