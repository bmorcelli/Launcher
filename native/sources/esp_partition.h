// Native shim: only an opaque type is needed — nothing in the UI code paths
// this harness exercises actually reads or writes a partition.
#pragma once

#include <cstdint>

struct esp_partition_t {
    uint8_t type;
    uint8_t subtype;
    uint32_t address;
    uint32_t size;
    char label[16];
};
