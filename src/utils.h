#ifndef UTILS_H
#define UTILS_H

#include <ArduinoJson.h>

// Custom ArduinoJson allocator for large, transient documents (the online
// firmware/update lists). It routes ArduinoJson's many small node/string
// allocations to PSRAM when present, keeping that churn off the internal heap;
// on boards without PSRAM it falls back to internal RAM. Paired with
// buildFirmwareListFilter() (which slashes how many nodes get created in the
// first place), this is the "A+B" anti-fragmentation pair for `doc`.
ArduinoJson::Allocator *launcherJsonAllocator();

// Fills `filter` with only the fields the UI actually reads from the
// firmwares / updateList response. Passed as DeserializationOption::Filter so
// deserializeJson keeps just these, keeping the parsed document small.
void buildFirmwareListFilter(JsonDocument &filter);

#endif
