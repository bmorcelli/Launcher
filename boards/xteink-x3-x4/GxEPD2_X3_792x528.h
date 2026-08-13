// GxEPD2 panel class for the Xteink X3's 792x528 e-paper.
//
// GxEPD2 has no class for this panel, and the X4's GxEPD2_426_GDEQ0426T82 is
// not a starting point: despite the two devices sharing a board and a pinout,
// the X3 is not an SSD1677. It carries a UC8253, whose command set (0x00 panel
// setting, 0x61 resolution, 0x10/0x13 data planes, 0x12 trigger, 0x20-0x24
// waveform LUTs) is the UltraChip KW family, its BUSY line is active LOW, and
// its gates scan bottom to top. So this is modelled on GxEPD2_750_T7 (the
// UC8179 800x480 class, same family) with the register sequence, the LUTs and
// the update policy taken from the open-x4 community SDK's _x3Mode paths:
//   https://github.com/open-x4-epaper/community-sdk
//
// This covers the ORIGINAL X3 batch only. Newer units ship a UC8279d, which
// runs pure OTP waveforms and would want no LUT upload at all — see the panel
// variants note in boards/xteink-x3-x4/platformio.ini.
//
// Untested on hardware — nobody here has an X3. Everything below is a faithful
// transcription of code that is known to drive the panel, but transcription is
// not verification.
//
// GxEPD2 is GPL-3.0 and this file is derived from it, so it carries the same
// licence — as does any firmware built with it.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _GxEPD2_X3_792x528_H_
#define _GxEPD2_X3_792x528_H_

#include <GxEPD2_EPD.h>

class GxEPD2_X3_792x528 : public GxEPD2_EPD {
public:
    // attributes
    static const uint16_t WIDTH = 792;
    static const uint16_t WIDTH_VISIBLE = WIDTH;
    static const uint16_t HEIGHT = 528;
    // No enumerator of our own: GxEPD2::Panel lives in the library and this
    // board has no business editing it. The value is informational.
    static const GxEPD2::Panel panel = GxEPD2::GDEW075T7;
    static const bool hasColor = false;
    static const bool hasPartialUpdate = true;
    static const bool usePartialUpdateWindow = false;
    static const bool hasFastPartialUpdate = true;
    static const uint16_t power_on_time = 150;       // ms
    static const uint16_t power_off_time = 150;      // ms
    static const uint16_t full_refresh_time = 3000;  // ms
    static const uint16_t partial_refresh_time = 800; // ms

    GxEPD2_X3_792x528(int16_t cs, int16_t dc, int16_t rst, int16_t busy);

    void clearScreen(uint8_t value = 0xFF);
    void writeScreenBuffer(uint8_t value = 0xFF);

    void writeImage(
        const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
        bool mirror_y = false, bool pgm = false
    );
    void writeImageToPrevious(
        const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
        bool mirror_y = false, bool pgm = false
    );
    void writeImage(
        const uint8_t *black, const uint8_t *color, int16_t x, int16_t y, int16_t w, int16_t h,
        bool invert = false, bool mirror_y = false, bool pgm = false
    );
    void writeNative(
        const uint8_t *data1, const uint8_t *data2, int16_t x, int16_t y, int16_t w, int16_t h,
        bool invert = false, bool mirror_y = false, bool pgm = false
    );
    void drawImage(
        const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
        bool mirror_y = false, bool pgm = false
    );
    void drawImage(
        const uint8_t *black, const uint8_t *color, int16_t x, int16_t y, int16_t w, int16_t h,
        bool invert = false, bool mirror_y = false, bool pgm = false
    );
    void drawNative(
        const uint8_t *data1, const uint8_t *data2, int16_t x, int16_t y, int16_t w, int16_t h,
        bool invert = false, bool mirror_y = false, bool pgm = false
    );

    // The controller copies new to old itself (N2OCP, set through 0x50), so
    // there is nothing to do here — same as the stock UC8179 classes.
    void writeImageAgain(
        const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false,
        bool mirror_y = false, bool pgm = false
    ) {}
    void writeImagePartAgain(
        const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
        bool pgm = false
    ) {}

    // Sub-screen writes. The launcher's backend never issues one — it paints a
    // full page buffer and calls display() — and the SDK has no windowed write
    // path to copy, so rather than guess at 0x90/0x91/0x92 geometry on a panel
    // nobody can test, these promote to a full-screen write when the region
    // covers the screen and are ignored otherwise.
    void writeImagePart(
        const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
        bool pgm = false
    );
    void writeImagePartToPrevious(
        const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
        bool pgm = false
    );
    void writeImagePart(
        const uint8_t *black, const uint8_t *color, int16_t x_part, int16_t y_part,
        int16_t w_bitmap, int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h,
        bool invert = false, bool mirror_y = false, bool pgm = false
    );
    void drawImagePart(
        const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false,
        bool pgm = false
    );
    void drawImagePart(
        const uint8_t *black, const uint8_t *color, int16_t x_part, int16_t y_part,
        int16_t w_bitmap, int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h,
        bool invert = false, bool mirror_y = false, bool pgm = false
    );

    void refresh(bool partial_update_mode = false);
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h);
    void powerOff();
    void hibernate();

private:
    // Which plane to paint is decided at refresh() time, not at write time:
    // the X3 sends a different data polarity and a different LUT set for a
    // full sync than for a differential one, and GxEPD2 only reveals which
    // kind of refresh is wanted after the image has been handed over. So the
    // write is recorded here and replayed once the mode is known. It is a
    // borrowed pointer — GxEPD2_BW's page buffer outlives every call.
    const uint8_t *_pending;
    uint8_t _pending_fill; // used when _pending is null (constant plane)
    bool _pending_pgm;
    bool _pending_invert;
    bool _pending_mirror;
    bool _old_ram_valid; // 0x10 holds the previous frame, so a diff is possible
    bool _panel_configured;

    void _recordPlane(const uint8_t *bitmap, uint8_t fill, bool invert, bool mirror_y, bool pgm);
    void _sendPlane(uint8_t command, bool invert);
    void _writeLutSet(bool image_mode);
    void _InitDisplay();
    void _PowerOn();
    void _PowerOff();

    static const uint8_t lut_vcom_full[];
    static const uint8_t lut_ww_full[];
    static const uint8_t lut_bw_full[];
    static const uint8_t lut_wb_full[];
    static const uint8_t lut_bb_full[];
    static const uint8_t lut_vcom_img[];
    static const uint8_t lut_ww_img[];
    static const uint8_t lut_bw_img[];
    static const uint8_t lut_wb_img[];
    static const uint8_t lut_bb_img[];
};

#endif // _GxEPD2_X3_792x528_H_
