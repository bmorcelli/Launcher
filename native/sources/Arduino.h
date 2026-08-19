// Native shim for the bits of Arduino.h the Launcher's UI code touches
// outside of DisplayDrivers itself: Serial, millis()/delay().
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

class HardwareSerial {
public:
    void begin(unsigned long) {}
    void setRxBufferSize(size_t) {}
    void setTxTimeoutMs(uint32_t) {}
    void print(const char *s) {
        if (s) fputs(s, stdout);
    }
    void print(const String &s) { print(s.c_str()); }
    void println(const char *s) {
        print(s);
        fputc('\n', stdout);
    }
    void println(const String &s) { println(s.c_str()); }
    void println() { fputc('\n', stdout); }
    void printf(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
};
extern HardwareSerial Serial;

inline unsigned long millis() { return lgfx::millis(); }
inline void delay(unsigned long ms) { lgfx::delay(ms); }

#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef FILE_READ
#define FILE_READ "r"
#endif
#ifndef FILE_WRITE
#define FILE_WRITE "w"
#endif

// Arduino's global min/max templates.
template <typename T, typename U> auto min(T a, U b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template <typename T, typename U> auto max(T a, U b) -> decltype(a > b ? a : b) { return a > b ? a : b; }

// ESP32 Arduino core's esp32-hal-log.h logging macros.
#define log_e(...) ((void)0)
#define log_w(...) ((void)0)
#define log_i(...) ((void)0)
#define log_d(...) ((void)0)
#define log_v(...) ((void)0)
