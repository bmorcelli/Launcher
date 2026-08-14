// See xteink_panel_probe.h.
//
// SPDX-FileCopyrightText: 2026 bmorcelli
//
// SPDX-License-Identifier: MIT

#include "xteink_panel_probe.h"

#include <Arduino.h>

namespace {

// Mode 0, MSB first. digitalWrite is slow enough on its own that the clock
// lands far below anything these controllers mind.
void writeByte(const XteinkPanelPins &p, uint8_t b) {
    for (int8_t i = 7; i >= 0; --i) {
        digitalWrite(p.mosi, (b >> i) & 1);
        digitalWrite(p.sclk, HIGH);
        digitalWrite(p.sclk, LOW);
    }
}

uint8_t readByte(const XteinkPanelPins &p) {
    uint8_t v = 0;
    for (int8_t i = 7; i >= 0; --i) {
        digitalWrite(p.sclk, HIGH);
        v |= (uint8_t)((digitalRead(p.mosi) & 1) << i);
        digitalWrite(p.sclk, LOW);
    }
    return v;
}

} // namespace

XteinkSilicon xteinkProbeSilicon(const XteinkPanelPins &pins, uint8_t *ver_out) {
    if (ver_out) {
        for (uint8_t i = 0; i < 5; i++) ver_out[i] = 0;
    }

    pinMode(pins.sclk, OUTPUT);
    pinMode(pins.mosi, OUTPUT);
    pinMode(pins.dc, OUTPUT);
    pinMode(pins.cs, OUTPUT);
    pinMode(pins.busy, INPUT);
    digitalWrite(pins.sclk, LOW);
    digitalWrite(pins.cs, HIGH);
    digitalWrite(pins.dc, LOW);

    // Reset pulse, then let the controller settle. Both families come up idle
    // and unbusy from here; the panel driver resets again before it uses the
    // panel for real, so nothing is lost by doing it now.
    if (pins.rst >= 0) {
        pinMode(pins.rst, OUTPUT);
        digitalWrite(pins.rst, HIGH);
        delay(5);
        digitalWrite(pins.rst, LOW);
        delay(10);
        digitalWrite(pins.rst, HIGH);
    }
    delay(50);

    // Step 1: an SSD1677 idles BUSY low, an UltraChip part idles it high.
    if (digitalRead(pins.busy) == LOW) return XTEINK_SILICON_SSD1677;

    // Step 2: VER (0x70), read back half-duplex on MOSI.
    uint8_t ver[5] = {0, 0, 0, 0, 0};
    digitalWrite(pins.dc, LOW); // command
    digitalWrite(pins.cs, LOW);
    writeByte(pins, 0x70);
    digitalWrite(pins.dc, HIGH); // data
    pinMode(pins.mosi, INPUT);   // the controller drives the line from here on
    for (uint8_t i = 0; i < 5; i++) ver[i] = readByte(pins);
    digitalWrite(pins.cs, HIGH);
    pinMode(pins.mosi, OUTPUT);

    if (ver_out) {
        for (uint8_t i = 0; i < 5; i++) ver_out[i] = ver[i];
    }

    // ver[0] is reserved, ver[1] is CHIP_VER, ver[2..4] are LUT_VER. The UC8279
    // variants are the ones whose first LUT_VER byte is 0x02 or 0x68.
    if (ver[2] == 0x02 || ver[2] == 0x68) return XTEINK_SILICON_UC8279;
    return XTEINK_SILICON_ULTRACHIP;
}
