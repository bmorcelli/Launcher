// Which controller is behind the glass on an Xteink reader.
//
// Xteink changed panel controller mid-production on every model without
// changing the glass, the resolution or the pinout, and nothing at build time
// can tell you which one a unit has. This reads it off the panel at boot, the
// same way the FreeInk SDK does, so one binary can drive all of them.
//
// Two steps, in this order:
//
//   1. BUSY polarity. The SSD1677 flags busy HIGH and therefore idles LOW; every
//      UltraChip part in this family is the other way round. After a reset pulse
//      with no command pending, that one pin separates the original X4 from
//      everything that replaced it, with no bus traffic at all.
//
//   2. The VER register (0x70), read half-duplex on the MOSI line — the panel is
//      write-only, there is no MISO on this bus, so the controller drives back
//      on the pin the host just wrote the command on. It answers with a reserved
//      byte, CHIP_VER, and a 24-bit LUT_VER; the UC8279 variants are the ones
//      whose first LUT_VER byte reads 0x02 or 0x68.
//
// Step 2 is the weaker of the two. The raw bytes are handed back so the caller
// can log them: if a unit is misidentified in the field, that log is what turns
// the report into a fix. Callers should treat XTEINK_SILICON_ULTRACHIP as "the
// UltraChip part this model originally shipped", which keeps an unrecognised
// answer on the behaviour the device had before runtime selection existed.
//
// Call this before anything else claims the display pins — it drives them
// directly and hands them back as outputs, with CS high.
//
// SPDX-FileCopyrightText: 2026 bmorcelli
//
// SPDX-License-Identifier: MIT

#ifndef XTEINK_PANEL_PROBE_H
#define XTEINK_PANEL_PROBE_H

#include <stdint.h>

enum XteinkSilicon : uint8_t {
    XTEINK_SILICON_SSD1677 = 0, // BUSY idles low
    XTEINK_SILICON_UC8279,      // UltraChip, LUT_VER byte reads 0x02 or 0x68
    XTEINK_SILICON_ULTRACHIP,   // UltraChip, anything else: the model's first one
};

struct XteinkPanelPins {
    int8_t cs, dc, rst, busy, sclk, mosi;
};

// ver_out, when not null, receives the five VER bytes as they came off the wire.
// It is zeroed when the probe stops at step 1 (an SSD1677 never gets asked).
XteinkSilicon xteinkProbeSilicon(const XteinkPanelPins &pins, uint8_t *ver_out = nullptr);

#endif // XTEINK_PANEL_PROBE_H
