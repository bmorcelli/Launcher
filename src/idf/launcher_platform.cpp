#include "launcher_platform.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <cstdarg>

void launcherConsolePrintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Serial.vprintf(fmt, args);
    va_end(args);
}

void launcherConsolePrint(const char *text) { Serial.print(text); }

void launcherConsolePrintln(const char *text) { Serial.println(text); }

void launcherConsolePrintLong(const char *text) {
    String payload(text);
    payload += '\n';
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(2000);
#endif
    Serial.write(reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length());
    Serial.flush();
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);
#endif
}

void launcherConsoleBegin(unsigned long baud) { Serial.begin(baud); }

void launcherConsoleFlush() { Serial.flush(); }

void launcherConsoleEnd() { Serial.end(); }
