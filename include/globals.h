// Globals.h
// https://github.com/espressif/esp-idf/blob/master/components/soc/esp32/include/soc/reset_reasons.h for
// further refference
#ifndef GLOBALS_H
#define GLOBALS_H
#include "pins_arduino.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h> // to make M5GFX compile in Core, Core2 and CoreS3 devices
#include <functional>
#include <interface.h>
#include <pre_compiler.h>
#include <vector>
#if !defined(SDM)
#define SDM SD
#define SDM_SD
#endif

#define KEY_ESCAPE 0x1B
#define NO_COLOR 1 // color is from 0x0000 (BLACK) to 0xffff (WHITE), 1 is for checking

struct Option {
    String label;
    std::function<void()> operation;
    uint16_t color;

    Option(String lbl = "", const std::function<void()> &op = nullptr, uint16_t color = NO_COLOR)
        : label(lbl), operation(op), color(color) {}
};

extern std::vector<Option> options;

struct MenuOptions {
    String name;
    String text;
    std::function<void()> action;
    bool active;
    bool selected;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;

    MenuOptions(
        String name, String text, std::function<void()> action, bool active = true, bool selected = false,
        uint16_t x = 0, uint16_t y = 0, uint16_t w = 0, uint16_t h = 0
    )
        : name(name), text(text), action(action), active(active), selected(selected), x(x), y(y), w(w), h(h) {
    }
    MenuOptions()
        : name(""), text(""), action(nullptr), active(true), selected(false), x(0), y(0), w(0), h(0) {}

    bool contain(int x, int y) {
        return this->x <= x && x < (this->x + this->w) && this->y <= y && y < (this->y + this->h);
    }
    void resetCoords() {
        x = 0;
        y = 0;
        w = 0;
        h = 0;
    }
    void setCoords(int x, int y, int w, int h) {
        this->x = x;
        this->y = y;
        this->w = w;
        this->h = h;
    }
};
struct keyStroke { // DO NOT CHANGE IT!!!!!
    bool pressed = false;
    bool exit_key = false;
    bool fn = false;
    bool del = false;
    bool enter = false;
    uint8_t modifiers = 0;
    std::vector<char> word;
    std::vector<uint8_t> hid_keys;
    std::vector<uint8_t> modifier_keys;

    // Clear function
    void Clear() {
        pressed = false;
        exit_key = false;
        fn = false;
        del = false;
        enter = false;
        modifiers = 0;
        word.clear();
        hid_keys.clear();
        modifier_keys.clear();
    }
};

struct TouchPoint {
    bool pressed = false;
    uint16_t x;
    uint16_t y;

    // clear touch to better handle tasks
    void Clear(void) {
        pressed = false;
        x = 0;
        y = 0;
    }
};

extern TouchPoint touchPoint;
extern keyStroke KeyStroke;
// Navigation Variables
extern long LongPressTmp;
extern volatile bool LongPress;
extern volatile bool NextPress;
extern volatile bool PrevPress;
extern volatile bool UpPress;
extern volatile bool DownPress;
extern volatile bool SelPress;
extern volatile bool EscPress;
extern volatile bool AnyKeyPress;

inline void resetGlobals(void) {
    NextPress = false;
    PrevPress = false;
    UpPress = false;
    DownPress = false;
    SelPress = false;
    EscPress = false;
    AnyKeyPress = false;
    touchPoint.Clear();
    KeyStroke.Clear();
}

extern volatile uint16_t tftHeight;
extern volatile uint16_t tftWidth;

extern TaskHandle_t xHandle;
extern inline bool check(volatile bool &btn) {
#ifndef DONT_USE_INPUT_TASK
    if (!btn) return false;
    btn = false;
    AnyKeyPress = false;
    return true;
#else
    static uint8_t count = 0;
    if (count > 5) {
        InputHandler();
        count = 0;
    }
    count++;
    if (!btn) return false;
    btn = false;
    AnyKeyPress = false;
    return true;
#endif
}

#define U_FAT_vfs 300
#define U_FAT_sys 400

extern uint16_t FGCOLOR;
extern uint16_t ALCOLOR;
extern uint16_t BGCOLOR;
extern uint16_t odd_color;
extern uint16_t even_color;

extern uint32_t MAX_APP;
extern uint32_t MAX_SPIFFS;
extern uint32_t MAX_FAT_vfs;
extern uint32_t MAX_FAT_sys;

// Screen sleep control variables
extern unsigned long previousMillis;
extern bool isSleeping;
extern bool isScreenOff;
extern bool dev_mode;

extern int dimmerSet;
extern int bright;
extern bool dimmer;

extern int prog_handler; // 0 - Flash, 1 - SPIFFS, 2 - Download

extern bool sdcardMounted;

extern String ssid;

extern String pwd;

extern String wui_usr;

extern String wui_pwd;

extern String dwn_path;

extern int currentIndex;

extern uint16_t total_firmware; // Number of available firmware on the list

extern uint8_t current_page; // Current Page

extern uint8_t num_pages; // Number of pages (total fw/fw per page)

extern JsonDocument doc;

extern JsonArray favorite;

extern JsonDocument settings;

extern String fileToCopy;

extern bool onlyBins;

extern int rotation;

extern bool returnToMenu;

extern uint8_t buff[1024];

extern const int bufSize;

// Used to handle the update in webUI
extern bool update;

// Used to choose SPIFFS or not
extern bool askSpiffs;

// size o the file in the webInterface
extern size_t file_size;

#if defined(HEADLESS)
extern uint8_t _miso;
extern uint8_t _mosi;
extern uint8_t _sck;
extern uint8_t _cs;
#endif

#endif
