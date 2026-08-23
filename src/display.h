// display.h
#ifndef __DISPLAY_H
#define __DISPLAY_H

#include <DisplayDrivers.h>

#include <ArduinoJson.h>
#include <functional>
#include <globals.h>
#include <vector>

// Short colour names, the way the launcher has always written them. The panel
// backends only publish the TFT_* spellings, and on the e-paper ones these are
// mapped to panel levels when they are drawn.
#define BLACK TFT_BLACK
#define NAVY TFT_NAVY
#define DARKGREEN TFT_DARKGREEN
#define DARKCYAN TFT_DARKCYAN
#define MAROON TFT_MAROON
#define PURPLE TFT_PURPLE
#define OLIVE TFT_OLIVE
#define LIGHTGREY TFT_LIGHTGREY
#define DARKGREY TFT_DARKGREY
#define BLUE TFT_BLUE
#define GREEN TFT_GREEN
#define CYAN TFT_CYAN
#define RED TFT_RED
#define MAGENTA TFT_MAGENTA
#define YELLOW TFT_YELLOW
#define WHITE TFT_WHITE
#define ORANGE TFT_ORANGE
#define GREENYELLOW TFT_GREENYELLOW
#define PINK TFT_PINK
#define PALERED 0xF9A0

#if defined(E_PAPER_DISPLAY) && defined(USE_M5GFX)
#define NATIVE_BGCOLOR WHITE
#elif defined(E_PAPER_DISPLAY)
#define NATIVE_BGCOLOR WHITE
#else
#define NATIVE_BGCOLOR BLACK
#endif

inline int16_t panelWidth() { return displayConfig.width; }
inline int16_t panelHeight() { return displayConfig.height; }

// Declaração dos objetos TFT
extern tft_display *tft;

#define FREE_TFT delete tft;

int loopOptions(
    std::vector<Option> &options, bool bright = false, uint16_t al = ALCOLOR, uint16_t bg = BGCOLOR,
    bool border = true, int index = 0
);
inline int loopOptions(
    int index, std::vector<Option> &options, uint16_t al = ALCOLOR, uint16_t bg = BGCOLOR, bool border = true
) {
    return loopOptions(options, false, al, bg, border, index);
}
void loopVersions(const String &fid);
void loopFirmware(bool isUpdate = false);
void initDisplay(bool doAll = false); // Início da função e mostra bootscreen
void initDisplayLoop();

// Funções para economizar linhas nas outras funções
void resetTftDisplay(
    int x = 0, int y = 0, uint16_t fc = FGCOLOR, int size = _fm, uint16_t bg = BGCOLOR,
    uint16_t screen = BGCOLOR
);
void setTftDisplay(
    int x = 0, int y = 0, uint16_t fc = tft->getTextColor(), int size = tft->getTextSize(),
    uint16_t bg = tft->getTextBgColor()
);

void displayCurrentVersion(
    const String &name, const String &author, const String &version, const String &published_at,
    int versionIndex, JsonArray versions
);
uint16_t getComplementaryColor(uint16_t color);
// keepAwake: a stripe is normally put up to be read, so it restarts the screen-off
// timer. Pass false where a stripe is repainted on a timer by something that wants
// the screen to go dark anyway — see chargeMode().
void displayRedStripe(
    const String &text, uint16_t fgcolor = getComplementaryColor(BGCOLOR), uint16_t bgcolor = ALCOLOR,
    bool keepAwake = true
);

void displayMsg(String txt, bool waitKeyPress = false); // Red Stripe + wait/delay

void displayError(String txt, bool waitKeyPress = false); // Red Stripe + wait/delay

void progressHandler(size_t progress, size_t total);

struct Opt_Coord {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t size = 10;
    uint16_t boxX = 0;
    uint16_t boxY = 0;
    uint16_t boxW = 0;
    uint16_t boxH = 0;
    uint16_t fgcolor = FGCOLOR;
    uint16_t bgcolor = BGCOLOR;
};
void displayScrollingText(const String &text, Opt_Coord &coord);

// Opt_Coord drawOptions(int index,Option& options,
// uint16_t fgcolor, uint16_t bgcolor);
Opt_Coord drawOptions(
    int index, std::vector<Option> &options, std::vector<MenuOptions> &opt, uint16_t fgcolor,
    uint16_t bgcolor, bool border, bool forceFullRedraw = true
);

void drawDeviceBorder();

void drawBatteryStatus(uint8_t bat);

void drawWifiStatus(bool hasBattery = false);

void drawMainMenu(std::vector<MenuOptions> &opt, int index, bool forceFullRedraw = true);

// Draws the installed-app shortcut cards at the top of the bootscreen (touch tap or
// keyboard digit boots that app directly) and returns the total height, in pixels,
// occupied by the cards, so callers can use it as a top margin for content below.
int drawBootAppShortcuts(std::vector<MenuOptions> &opt);

// Lazily-built, cached list of the shortcut cards (label = app icon text, action =
// boot into that app). Shared between initDisplay (which draws them) and the
// bootscreen input loop (which hit-tests touches against the same coordinates).
std::vector<MenuOptions> &launcherBootAppShortcuts();

void TouchFooter(uint16_t color = FGCOLOR);

void TouchFooter2(uint16_t color = FGCOLOR);

// Draw a single character at x,y in explicit colours, leaving the current text
// colours untouched. DisplayDrivers has no drawChar of its own — a per-glyph
// call is not something every backend can express the same way — so this goes
// through drawString, which they all agree on.
void drawCharAt(int32_t x, int32_t y, char c, uint16_t fg, uint16_t bg);

void tftprintln(const String &txt, int margin, int numlines = 0);

void tftprint(const String &txt, int margin, int numlines = 0);

#endif
