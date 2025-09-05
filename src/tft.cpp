#include "tft.h"

#if defined(E_PAPER_DISPLAY) && !defined(GxEPD2_DISPLAY)

#elif defined(HEADLESS)

#elif defined(USE_LOVYANGFX)

#elif defined(USE_TFT_ESPI)

#elif defined(USE_M5GFX)

#else
void Ard_eSPI::drawCentreString(String s, uint16_t x, uint16_t y, int f) {
    uint16_t _x = cursor_x;
    uint16_t _y = cursor_y;
    uint16_t w, h;
    int16_t x1, y1;
    getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    setCursor(x - w / 2, y);
    this->print(s);
    setCursor(_x, _y);
}

void Ard_eSPI::drawString(String s, uint16_t x, uint16_t y) {
    uint16_t _x = cursor_x;
    uint16_t _y = cursor_y;
    uint16_t w, h;
    int16_t x1, y1;
    getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    setCursor(x, y);
    this->print(s);
    setCursor(_x, _y);
}

void Ard_eSPI::drawRightString(String s, uint16_t x, uint16_t y, int f) {
    uint16_t _x = cursor_x;
    uint16_t _y = cursor_y;
    uint16_t w, h;
    int16_t x1, y1;
    getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    setCursor(x - w, y);
    this->print(s);
    setCursor(_x, _y);
}
#endif
