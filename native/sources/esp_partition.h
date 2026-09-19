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

enum esp_partition_type_t { ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1 };
#define ESP_PARTITION_SUBTYPE_ANY 0xFF

inline const esp_partition_t *esp_partition_find_first(esp_partition_type_t, uint8_t, const char *) {
    return nullptr;
}
