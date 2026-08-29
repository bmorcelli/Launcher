#include "launcher_platform.h"

#include <Arduino.h>
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
    if (!text) return;
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(2000);
#endif
    const uint8_t *payload = reinterpret_cast<const uint8_t *>(text);
    size_t remaining = strlen(text);
    uint32_t deadline = launcherMillis() + 2000;
    while (remaining > 0) {
        size_t chunk = remaining > 64 ? 64 : remaining;
        size_t written = Serial.write(payload, chunk);
        if (written > 0) {
            payload += written;
            remaining -= written;
            deadline = launcherMillis() + 2000;
        } else {
            if ((int32_t)(launcherMillis() - deadline) >= 0) break;
            launcherDelayMs(1);
        }
    }
    Serial.println();
    Serial.flush();
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);
#endif
}

void launcherConsoleBegin(unsigned long baud) { Serial.begin(baud); }

void launcherConsoleFlush() { Serial.flush(); }

void launcherConsoleEnd() { Serial.end(); }
