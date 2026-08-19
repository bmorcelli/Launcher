// Native shim: no real SD card on the desktop build.
#pragma once

#include "FS.h"

class SDClass : public FS {
public:
    bool begin(int = -1) { return false; }
    uint64_t cardSize() { return 0; }
    uint64_t usedBytes() { return 0; }
};
extern SDClass SD;
