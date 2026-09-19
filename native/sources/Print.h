// Native shim for Arduino's Print.h — just enough that File (FS.h) can
// derive from it via Stream, so ArduinoJson's is_base_of<Print, T> writer
// specialization picks File up the same way it would on real hardware.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size) {
        size_t n = 0;
        while (n < size && write(buffer[n])) n++;
        return n;
    }
    size_t write(const char *s) { return s ? write(reinterpret_cast<const uint8_t *>(s), strlen(s)) : 0; }
};

class Printable {
public:
    virtual ~Printable() = default;
    virtual size_t printTo(Print &p) const = 0;
};
