// Native shim for esp-idf's esp_mac.h.
#pragma once

#include <cstdint>
#include <cstring>

inline int esp_efuse_mac_get_default(uint8_t *mac) {
    if (mac) std::memset(mac, 0, 6);
    return 0;
}
