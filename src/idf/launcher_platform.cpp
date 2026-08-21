#include "launcher_platform.h"

#include <HardwareSerial.h>
#include <cstdarg>
#include <cstring>

void launcherConsolePrintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Serial.vprintf(fmt, args);
    va_end(args);
}

void launcherConsolePrint(const char *text) { Serial.print(text); }

void launcherConsolePrintln(const char *text) { Serial.println(text); }

void launcherConsolePrintLong(const char *text) {
    size_t total = strlen(text);
    size_t sent = 0;
    uint32_t stallStartMs = 0;
    while (sent < total) {
        size_t n = Serial.write(reinterpret_cast<const uint8_t *>(text) + sent, total - sent);
        if (n > 0) {
            sent += n;
            stallStartMs = 0;
            continue;
        }
        if (stallStartMs == 0) stallStartMs = launcherMillis();
        if (launcherMillis() - stallStartMs > 3000) break; // host is gone, stop retrying
        launcherDelayMs(5);
    }
    Serial.write('\n');
    Serial.flush();
}

void launcherConsoleBegin(unsigned long baud) { Serial.begin(baud); }

void launcherConsoleFlush() { Serial.flush(); }

void launcherConsoleEnd() { Serial.end(); }
