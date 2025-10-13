// display.h
#ifndef __DISPLAY_H
#define __DISPLAY_H

#ifdef HEADLESS
#include <VectorDisplay.h>
#else
#include <tft.h>
#endif
#include <ArduinoJson.h>
#include <functional>
#include <globals.h>
#include <vector>
// Declaração dos objetos TFT
#if defined(HEADLESS)
extern SerialDisplayClass *tft;
#else
extern Ard_eSPI *tft;
#endif

#define FREE_TFT delete tft;

int loopOptions(
    std::vector<Option> &options, bool bright = false, uint16_t al = RED, uint16_t bg = BLACK,
    bool border = true, int index = 0
);
void loopVersions(String fid);
void loopFirmware();
void initDisplay(bool doAll = false); // Início da função e mostra bootscreen
void initDisplayLoop();

// Funções para economizar linhas nas outras funções
void resetTftDisplay(
    int x = 0, int y = 0, uint16_t fc = FGCOLOR, int size = FM, uint16_t bg = BGCOLOR,
    uint16_t screen = BGCOLOR
);
void setTftDisplay(
    int x = 0, int y = 0, uint16_t fc = tft->getTextcolor(), int size = tft->getTextsize(),
    uint16_t bg = tft->getTextbgcolor()
);

void displayCurrentVersion(
    String name, String author, String version, String published_at, int versionIndex, JsonArray versions
);
uint16_t getComplementaryColor(uint16_t color);
void displayRedStripe(
    String text, uint16_t fgcolor = getComplementaryColor(BGCOLOR), uint16_t bgcolor = ALCOLOR
);

void progressHandler(size_t progress, size_t total);

struct Opt_Coord {
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t size = 10;
    uint16_t fgcolor = FGCOLOR;
    uint16_t bgcolor = BGCOLOR;
};
void displayScrollingText(const String &text, Opt_Coord &coord);

// Opt_Coord drawOptions(int index,Option& options,
// uint16_t fgcolor, uint16_t bgcolor);
Opt_Coord drawOptions(
    int index, std::vector<Option> &options, std::vector<MenuOptions> &opt, uint16_t fgcolor,
    uint16_t bgcolor, bool border
);

void drawDeviceBorder();

void drawBatteryStatus(uint8_t bat);

void drawMainMenu(std::vector<MenuOptions> &opt, int index);

void TouchFooter(uint16_t color = FGCOLOR);

void TouchFooter2(uint16_t color = FGCOLOR);

void tftprintln(String txt, int margin, int numlines = 0);

void tftprint(String txt, int margin, int numlines = 0);

#endif
