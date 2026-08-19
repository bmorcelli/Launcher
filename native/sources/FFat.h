// Native shim: no real FAT partition on the desktop build.
#pragma once

#include "FS.h"

class FFatFS : public FS {
public:
    bool begin(bool = false) { return false; }
    uint64_t totalBytes() { return 0; }
    uint64_t usedBytes() { return 0; }
};
extern FFatFS FFat;
