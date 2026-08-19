// Native shim: no real SPIFFS partition on the desktop build.
#pragma once

#include "FS.h"

class SPIFFSFS : public FS {
public:
    bool begin(bool = false, const char * = "/spiffs", uint8_t = 10, const char * = "spiffs") {
        return false;
    }
    uint64_t totalBytes() { return 0; }
    uint64_t usedBytes() { return 0; }
};
extern SPIFFSFS SPIFFS;
