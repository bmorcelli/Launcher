// Native shim: just enough of Arduino's Stream to satisfy signatures that
// take one by reference. Inherits Print (like the real Arduino core) so
// File (FS.h) picks up ArduinoJson's is_base_of<Print, T> writer.
#pragma once

#include "Print.h"

class Stream : public Print {
public:
    virtual ~Stream() = default;
    virtual size_t readBytes(char *buffer, size_t length) = 0;
};
