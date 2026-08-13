// See GxEPD2_X3_792x528.h for provenance and licence.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GxEPD2_X3_792x528.h"

// LUT sets, 42 bytes each, lifted verbatim from the open-x4 community SDK.
//
// "full" drives the differential refresh, "img" the full sync. The two are not
// interchangeable: the img set expects the data planes inverted, the full set
// expects them as-is, which is why the polarity below tracks the LUT choice.

const uint8_t GxEPD2_X3_792x528::lut_vcom_full[] PROGMEM = {
    0x00, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GxEPD2_X3_792x528::lut_ww_full[] PROGMEM = {
    0x20, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GxEPD2_X3_792x528::lut_bw_full[] PROGMEM = {
    0xAA, 0x06, 0x02, 0x06, 0x06, 0x01, 0x80, 0x05, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GxEPD2_X3_792x528::lut_wb_full[] PROGMEM = {
    0x55, 0x06, 0x02, 0x06, 0x06, 0x01, 0x40, 0x05, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GxEPD2_X3_792x528::lut_bb_full[] PROGMEM = {
    0x10, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const uint8_t GxEPD2_X3_792x528::lut_vcom_img[] PROGMEM = {
    0x00, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x00, 0x0C, 0x02, 0x07, 0x02, 0x01, 0x00, 0x01,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GxEPD2_X3_792x528::lut_ww_img[] PROGMEM = {
    0xA8, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x44, 0x0C, 0x02, 0x07, 0x02, 0x01, 0x04, 0x01,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GxEPD2_X3_792x528::lut_bw_img[] PROGMEM = {
    0x80, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x62, 0x0C, 0x02, 0x07, 0x02, 0x01, 0x00, 0x01,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GxEPD2_X3_792x528::lut_wb_img[] PROGMEM = {
    0x88, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x60, 0x0C, 0x02, 0x07, 0x02, 0x01, 0x00, 0x01,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GxEPD2_X3_792x528::lut_bb_img[] PROGMEM = {
    0x00, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x4A, 0x0C, 0x02, 0x07, 0x02, 0x01, 0x88, 0x01,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

GxEPD2_X3_792x528::GxEPD2_X3_792x528(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
    // BUSY is active LOW on this controller, unlike the X4's SSD1677.
    : GxEPD2_EPD(
          cs, dc, rst, busy, LOW, 10000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate,
          hasFastPartialUpdate
      ),
      _pending(nullptr), _pending_fill(0xFF), _pending_pgm(false), _pending_invert(false),
      _pending_mirror(false), _old_ram_valid(false), _panel_configured(false) {}

void GxEPD2_X3_792x528::_recordPlane(
    const uint8_t *bitmap, uint8_t fill, bool invert, bool mirror_y, bool pgm
) {
    _pending = bitmap;
    _pending_fill = fill;
    _pending_invert = invert;
    _pending_mirror = mirror_y;
    _pending_pgm = pgm;
}

void GxEPD2_X3_792x528::_sendPlane(uint8_t command, bool invert) {
    const uint16_t wb = WIDTH / 8;
    _writeCommand(command);
    _startTransfer();
    for (uint16_t y = 0; y < HEIGHT; y++) {
        // The gates scan bottom to top, so the panel wants the last row first.
        const uint16_t srcY = _pending_mirror ? y : (uint16_t)(HEIGHT - 1 - y);
        for (uint16_t xb = 0; xb < wb; xb++) {
            uint8_t data = _pending_fill;
            if (_pending != nullptr) {
                const uint32_t idx = (uint32_t)srcY * wb + xb;
                data = _pending_pgm ? pgm_read_byte(&_pending[idx]) : _pending[idx];
            }
            if (_pending_invert != invert) data = ~data;
            _transfer(data);
        }
    }
    _endTransfer();
}

void GxEPD2_X3_792x528::_writeLutSet(bool image_mode) {
    _writeCommand(0x20);
    _writeDataPGM(image_mode ? lut_vcom_img : lut_vcom_full, 42);
    _writeCommand(0x21);
    _writeDataPGM(image_mode ? lut_ww_img : lut_ww_full, 42);
    _writeCommand(0x22);
    _writeDataPGM(image_mode ? lut_bw_img : lut_bw_full, 42);
    _writeCommand(0x23);
    _writeDataPGM(image_mode ? lut_wb_img : lut_wb_full, 42);
    _writeCommand(0x24);
    _writeDataPGM(image_mode ? lut_bb_img : lut_bb_full, 42);
}

void GxEPD2_X3_792x528::_InitDisplay() {
    if (_hibernating) {
        _reset();
        delay(50); // the SDK settles for 50ms after reset on this panel
        _panel_configured = false;
        _old_ram_valid = false;
    }
    if (_panel_configured) return;

    _writeCommand(0x00); // panel setting
    _writeData(0x3F);
    _writeData(0x08);
    _writeCommand(0x61); // resolution
    // 0x0318 x 0x0258 — the controller is told 792x600 while only 528 gates
    // carry pixels. That is what the stock firmware sends; the extra gates are
    // off-panel.
    _writeData(0x03);
    _writeData(0x18);
    _writeData(0x02);
    _writeData(0x58);
    _writeCommand(0x65); // gate/source start
    _writeData(0x00);
    _writeData(0x00);
    _writeData(0x00);
    _writeData(0x00);
    _writeCommand(0x03); // gate power
    _writeData(0x1D);
    _writeCommand(0x01); // power setting
    _writeData(0x07);
    _writeData(0x17);
    _writeData(0x3F);
    _writeData(0x3F);
    _writeData(0x17);
    _writeCommand(0x82); // vcom DC
    _writeData(0x1D);
    _writeCommand(0x06); // booster soft start
    _writeData(0x25);
    _writeData(0x25);
    _writeData(0x3C);
    _writeData(0x37);
    _writeCommand(0x30); // PLL
    _writeData(0x09);
    _writeCommand(0xE1);
    _writeData(0x02);
    _writeLutSet(false);

    _panel_configured = true;
    _power_is_on = false;
}

void GxEPD2_X3_792x528::_PowerOn() {
    if (!_power_is_on) {
        _writeCommand(0x04);
        _waitWhileBusy("_PowerOn", power_on_time);
    }
    _power_is_on = true;
}

void GxEPD2_X3_792x528::_PowerOff() {
    if (_power_is_on) {
        _writeCommand(0x02);
        _waitWhileBusy("_PowerOff", power_off_time);
    }
    _power_is_on = false;
}

void GxEPD2_X3_792x528::writeScreenBuffer(uint8_t value) {
    _initial_write = false;
    _recordPlane(nullptr, value, false, false, false);
}

void GxEPD2_X3_792x528::clearScreen(uint8_t value) {
    _recordPlane(nullptr, value, false, false, false);
    _old_ram_valid = false; // a clear is always a full sync
    refresh(false);
}

void GxEPD2_X3_792x528::writeImage(
    const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y,
    bool pgm
) {
    // Only whole-screen writes reach the panel; see the header.
    if ((x != 0) || (y != 0) || (w != int16_t(WIDTH)) || (h != int16_t(HEIGHT))) return;
    _recordPlane(bitmap, 0xFF, invert, mirror_y, pgm);
}

void GxEPD2_X3_792x528::writeImageToPrevious(
    const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y,
    bool pgm
) {
    // The controller keeps the previous frame itself (N2OCP), so seeding 0x10
    // by hand would only fight it.
    (void)bitmap;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)invert;
    (void)mirror_y;
    (void)pgm;
}

void GxEPD2_X3_792x528::writeImage(
    const uint8_t *black, const uint8_t *color, int16_t x, int16_t y, int16_t w, int16_t h,
    bool invert, bool mirror_y, bool pgm
) {
    (void)color; // monochrome panel
    writeImage(black, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_X3_792x528::writeNative(
    const uint8_t *data1, const uint8_t *data2, int16_t x, int16_t y, int16_t w, int16_t h,
    bool invert, bool mirror_y, bool pgm
) {
    (void)data2;
    writeImage(data1, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_X3_792x528::drawImage(
    const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y,
    bool pgm
) {
    writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
    refresh(true);
}

void GxEPD2_X3_792x528::drawImage(
    const uint8_t *black, const uint8_t *color, int16_t x, int16_t y, int16_t w, int16_t h,
    bool invert, bool mirror_y, bool pgm
) {
    writeImage(black, color, x, y, w, h, invert, mirror_y, pgm);
    refresh(true);
}

void GxEPD2_X3_792x528::drawNative(
    const uint8_t *data1, const uint8_t *data2, int16_t x, int16_t y, int16_t w, int16_t h,
    bool invert, bool mirror_y, bool pgm
) {
    writeNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
    refresh(true);
}

// --- sub-screen variants: promote to full screen or drop, never guess ---

void GxEPD2_X3_792x528::writeImagePart(
    const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm
) {
    if ((x_part == 0) && (y_part == 0) && (w_bitmap == int16_t(WIDTH)) &&
        (h_bitmap == int16_t(HEIGHT))) {
        writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
    }
}

void GxEPD2_X3_792x528::writeImagePartToPrevious(
    const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm
) {
    (void)bitmap;
    (void)x_part;
    (void)y_part;
    (void)w_bitmap;
    (void)h_bitmap;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)invert;
    (void)mirror_y;
    (void)pgm;
}

void GxEPD2_X3_792x528::writeImagePart(
    const uint8_t *black, const uint8_t *color, int16_t x_part, int16_t y_part, int16_t w_bitmap,
    int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y,
    bool pgm
) {
    (void)color;
    writeImagePart(black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_X3_792x528::drawImagePart(
    const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm
) {
    writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
    refresh(true);
}

void GxEPD2_X3_792x528::drawImagePart(
    const uint8_t *black, const uint8_t *color, int16_t x_part, int16_t y_part, int16_t w_bitmap,
    int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y,
    bool pgm
) {
    writeImagePart(
        black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm
    );
    refresh(true);
}

void GxEPD2_X3_792x528::refresh(int16_t x, int16_t y, int16_t w, int16_t h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    refresh(true);
}

void GxEPD2_X3_792x528::refresh(bool partial_update_mode) {
    _InitDisplay();

    // A differential update is only meaningful once 0x10 holds a frame the
    // controller put there itself.
    const bool full_sync = !partial_update_mode || !_old_ram_valid || _initial_refresh;

    if (full_sync) {
        _writeLutSet(true);
        _sendPlane(0x13, true); // new, inverted
        _sendPlane(0x10, true); // old, inverted — every pixel becomes a transition
        _writeCommand(0x50);    // VCOM and data interval, N2OCP on
        _writeData(0xA9);
        _writeData(0x07);
    } else {
        _writeLutSet(false);
        _sendPlane(0x13, false); // new only; 0x10 still holds the previous frame
        _writeCommand(0x50);
        _writeData(0x29);
        _writeData(0x07);
    }

    if (!_power_is_on || full_sync) _PowerOn();

    _writeCommand(0x12); // trigger
    _waitWhileBusy("_Update", full_sync ? full_refresh_time : partial_refresh_time);

    if (full_sync) delay(200);

    _old_ram_valid = true;
    _initial_refresh = false;
    _using_partial_mode = !full_sync;
}

void GxEPD2_X3_792x528::powerOff() { _PowerOff(); }

void GxEPD2_X3_792x528::hibernate() {
    _PowerOff();
    if (_rst >= 0) {
        _writeCommand(0x07); // deep sleep
        _writeData(0xA5);
        _hibernating = true;
        _panel_configured = false;
        _old_ram_valid = false;
    }
}
