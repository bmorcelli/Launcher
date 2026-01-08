#ifndef __TFT_H
#define __TFT_H
#if defined(E_PAPER_DISPLAY) && !defined(GxEPD2_DISPLAY) && !defined(USE_M5GFX)
#include <EPD_translate.h>
#define DARKGREY TFT_DARKGREY
#define BLACK TFT_BLACK
#define WHITE TFT_WHITE
#define RED TFT_RED
#define GREEN TFT_GREEN
#define DARKCYAN TFT_DARKCYAN
#define LIGHTGREY TFT_LIGHTGREY

class Ard_eSPI : public EPD_translate {
public:
    inline int getTextsize() { return textsize; };
    inline uint16_t getTextcolor() { return textcolor; };
    inline uint16_t getTextbgcolor() { return textbgcolor; };
    inline void drawChar2(uint32_t x, uint32_t y, char c, uint16_t a, uint16_t b) {
        EPD_translate::drawChar(c, x, y);
    };
    inline void drawArc(int a, int b, int c, int d, int e, int f, int g) {};
    inline void begin() { EPD_translate::init(); };
    void setFullWindow() {};
    inline void display(bool a = false) {};

private:
};
#elif defined(GxEPD2_DISPLAY)
#include <GxEPD2_BW.h>
// #include <Fonts/FreeMonoBold9pt7b.h>
#define BOARD_SPI_CS 34
#define BOARD_SPI_DC 35
#define BOARD_SPI_RST -1
#define BOARD_SPI_BUSY 37
#define BOARD_SPI_SCK 36
#define BOARD_SPI_MOSI 33

#define DARKGREY 0x8888
#define BLACK GxEPD_WHITE
#define RED GxEPD_WHITE
#define WHITE GxEPD_BLACK
#define GREEN 0xAAAA
#define DARKCYAN 0x6666
#define LIGHTGREY 0x4444

class Ard_eSPI : public GxEPD2_BW<GxEPD2_310_GDEQ031T10, GxEPD2_310_GDEQ031T10::HEIGHT> {
public:
    Ard_eSPI()
        : GxEPD2_BW<GxEPD2_310_GDEQ031T10, GxEPD2_310_GDEQ031T10::HEIGHT>(
              GxEPD2_310_GDEQ031T10(BOARD_SPI_CS, BOARD_SPI_DC, BOARD_SPI_RST, BOARD_SPI_BUSY)
          ) {}
    void begin() {
        init(
            115200, true, 2, false
        ); // USE THIS for Waveshare boards with "clever" reset circuit, 2ms reset pulse
        setFullWindow();
    }
    inline void drawChar2(int16_t x, int16_t y, char c, int16_t a, int16_t b) {
        drawChar(x, y, c, a, b, textsize_x);
    };
    void drawString(String s, uint16_t x, uint16_t y);
    void drawCentreString(String s, uint16_t x, uint16_t y, int f);
    void drawRightString(String s, uint16_t x, uint16_t y, int f);
    void drawArc(int16_t x, int16_t y, int16_t r, int16_t ir, int16_t sA, int16_t eA, int16_t fg) {};
    inline int getTextsize() { return textsize_x; };
    inline uint16_t getTextcolor() { return textcolor; };
    inline uint16_t getTextbgcolor() { return textbgcolor; };

    void stopCallback() { setFullWindow(); };
    void startCallback() {};
};

#elif defined(HEADLESS)
// do nothing

#elif defined(USE_TFT_ESPI)
#include <TFT_eSPI.h>
#define DARKGREY TFT_DARKGREY
#define BLACK TFT_BLACK
#define RED TFT_RED
#define GREEN TFT_GREEN
#define WHITE TFT_WHITE
#define DARKCYAN TFT_DARKCYAN
#define LIGHTGREY TFT_LIGHTGREY

class Ard_eSPI : public TFT_eSPI {
public:
    inline int getTextsize() { return textsize; };
    inline uint16_t getTextcolor() { return textcolor; };
    inline uint16_t getTextbgcolor() { return textbgcolor; };
    inline void drawChar2(int16_t x, int16_t y, char c, int16_t a, int16_t b) { TFT_eSPI::drawChar(c, x, y); }
    inline void drawArc(int16_t x, int16_t y, int16_t r, int16_t ir, int16_t sA, int16_t eA, int16_t fg) {
        TFT_eSPI::drawArc(x, y, r, ir, sA, eA, fg, TFT_BLACK, true);
    };
    inline void display(bool a = false) {};
};

#elif defined(USE_LOVYANGFX)
#include "driver/i2c.h"
#include <LovyanGFX.hpp>
#define DARKGREY TFT_DARKGREY
#define BLACK TFT_BLACK
#define RED TFT_RED
#define GREEN TFT_GREEN
#define LIGHTGREY TFT_LIGHTGREY
class Ard_eSPI : public lgfx::LGFX_Device {
    lgfx::LOVYAN_PANEL _panel_instance;
    lgfx::LOVYAN_BUS _bus_instance;

public:
    inline int getTextsize() { return _text_style.size_x; };
    inline uint16_t getTextcolor() { return _text_style.fore_rgb888; };
    inline uint16_t getTextbgcolor() { return _text_style.back_rgb888; };
    inline void drawChar2(int16_t x, int16_t y, char c, int16_t a, int16_t b) {
        lgfx::LGFX_Device::drawChar(x, y, c, a, b, _text_style.size_x);
    }
    inline void drawCentreString(String s, uint16_t x, uint16_t y, int f) {
        lgfx::LGFX_Device::drawCentreString(s, x, y);
    };
    inline void drawRightString(String s, uint16_t x, uint16_t y, int f) {
        lgfx::LGFX_Device::drawRightString(s, x, y);
    };
    inline void display(bool a = false) {};

    Ard_eSPI(void) {
        {
            auto cfg = _bus_instance.config();

#if LOVYAN_BUS == Bus_Parallel8
            cfg.freq_write = 16000000;
            cfg.pin_wr = TFT_WR;
            cfg.pin_rd = TFT_RD;
            cfg.pin_rs = TFT_DC; // D/C
            cfg.pin_d0 = TFT_D0;
            cfg.pin_d1 = TFT_D1;
            cfg.pin_d2 = TFT_D2;
            cfg.pin_d3 = TFT_D3;
            cfg.pin_d4 = TFT_D4;
            cfg.pin_d5 = TFT_D5;
            cfg.pin_d6 = TFT_D6;
            cfg.pin_d7 = TFT_D7;

#elif LOVYAN_BUS == Bus_SPI
            cfg.spi_host =
                TFT_SPI_MODE; // VSPI ESP32-S2,C3 : SPI2_HOST or SPI3_HOST / ESP32 : VSPI_HOST or HSPI_HOST
            // ※ ESP-IDFバージョンアップに伴い、VSPI_HOST ,
            // HSPI_HOSTの記述は非推奨になるため、エラーが出る場合は代わりにSPI2_HOST ,
            // SPI3_HOSTを使用してください。
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = true;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = TFT_SCLK;
            cfg.pin_mosi = TFT_MOSI;
            cfg.pin_miso = TFT_MISO;
            cfg.pin_dc = TFT_DC;

#elif LOVYAN_BUS == Bus_I2C
            cfg.i2c_port = TFT_I2C_PORT;    // (0 or 1)
            cfg.freq_write = TFT_I2C_WRITE; // 400000
            cfg.freq_read = TFT_I2C_READ;   // 400000
            cfg.pin_sda = TFT_SDA;          //
            cfg.pin_scl = TFT_SCL;          //
            cfg.i2c_addr = TFT_ADDR;        //
#endif

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = TFT_CS;
            cfg.pin_rst = TFT_RST;
            cfg.pin_busy = -1; // TFT_BUSY;
            cfg.memory_width = TFT_WIDTH;
            cfg.memory_height = TFT_HEIGHT;
            cfg.panel_width = TFT_WIDTH;
            cfg.panel_height = TFT_HEIGHT;
            cfg.offset_x = TFT_COL_OFS1;
            cfg.offset_y = TFT_ROW_OFS1;
            cfg.offset_rotation = TFT_ROTATION;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = TFT_INVERTED;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = TFT_BUS_SHARED;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};
#elif USE_M5GFX
#include <M5GFX.h>
#include <M5Unified.h>
#if defined(E_PAPER_DISPLAY)
extern M5Canvas sprite;
#define drv sprite
#else
#define drv M5.Display
#endif
class Ard_eSPI {
public:
    int _fsize;
    uint16_t _fg;
    uint16_t _bg;
    // Driver initilizer
    Ard_eSPI() {
        _fsize = 1;
        _fg = BLACK;
        _bg = BLACK;
    };
    inline void begin() {
#if defined(E_PAPER_DISPLAY)
        sprite.createSprite(M5.Display.width(), M5.Display.height());
#endif
    };
// E-Paper finctions
#if defined(E_PAPER_DISPLAY)
    void stopCallback() {};
    void startCallback() {};
    void display(bool a = false) {
        sprite.pushSprite(0, 0);
#if defined(ARDUINO_M5STACK_PAPER)
        sprite.deleteSprite();
        sprite.createSprite(M5.Display.width(), M5.Display.height());
#endif
    };
    void setFullWindow() {};
#else
    inline void display(bool a = false) {};
#endif

    // End of E-Paper functions
    inline int getTextsize() { return _fsize; };
    inline uint16_t getTextcolor() { return _fg; };
    inline uint16_t getTextbgcolor() { return _bg; };

    inline size_t drawChar2(int16_t x, int16_t y, char c, int16_t a, int16_t b) {
        return drv.drawChar(x, y, c, b, a, _fsize);
    }
    inline size_t drawCentreString(String s, uint16_t x, uint16_t y, int f) {
        return drv.drawCentreString(s, x, y);
    };
    inline size_t drawRightString(String s, uint16_t x, uint16_t y, int f) {
        return drv.drawRightString(s, x, y);
    };
    inline void fillScreen(uint16_t c) { return drv.fillScreen(c); }
    inline void setRotation(int rot) { return drv.setRotation(rot); };
    inline void setTextColor(uint16_t fgcolor, uint16_t bgcolor) {
        _fg = fgcolor;
        _bg = bgcolor;
        return drv.setTextColor(fgcolor, bgcolor);
    }
    inline void setTextColor(uint16_t fgcolor) {
        _fg = fgcolor;
        return drv.setTextColor(fgcolor);
    }
    inline void setCursor(int32_t x, int32_t y) { return drv.setCursor(x, y); };
    inline void setTextSize(uint32_t c) {
        _fsize = c;
        return drv.setTextSize(c);
    }
    inline int32_t getCursorX(void) const { return drv.getCursorX(); }
    inline int32_t getCursorY(void) const { return drv.getCursorY(); }
    inline void drawRoundRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint16_t c) {
        return drv.drawRoundRect(x, y, w, h, r, c);
    }
    inline void fillRoundRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint16_t c) {
        return drv.fillRoundRect(x, y, w, h, r, c);
    }
    inline void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) {
        return drv.fillRect(x, y, w, h, c);
    }
    inline void drawRect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) {
        return drv.drawRect(x, y, w, h, c);
    }
    inline void drawArc(int16_t x, int16_t y, int16_t r, int16_t ir, int16_t sA, int16_t eA, int16_t fg) {
        return drv.drawArc(x, y, r, ir, float(sA + 90), float(eA + 90), fg);
    }
    inline void drawLine(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint16_t c) {
        return drv.drawLine(x0, y0, x1, y1, c);
    }
    inline void drawPixel(uint32_t x, uint32_t y, uint16_t c) { return drv.drawPixel(x, y, c); }
    inline size_t print(String a) { return drv.print(a); }
    inline size_t print(char a) { return drv.print(a); }
    inline size_t print(int a) { return drv.print(a); }
    inline size_t println(String a) { return drv.println(a); }
    inline size_t println() { return drv.println(); }
    inline int32_t width() { return drv.width(); }
    inline int32_t height() { return drv.height(); }
    inline size_t drawString(String s, int x, int y) { return drv.drawString(s, x, y); };
};

#else

#include <Arduino_GFX_Library.h>

#ifdef RGB_PANEL
#define TFT_BUS_TYPE Arduino_ESP32RGBPanel
#else
#define TFT_BUS_TYPE Arduino_DataBus
#endif

#if ST7789_DRIVER
#define _TFT_DRV Arduino_ST7789
#define _TFT_DRVF(a, b, c, d, e, f, g, h, i, j)                                                              \
    Arduino_ST7789(a, b, c, d, e, f, g, h, i, j) // it is not passing values greater than 255 for f
#elif ST7735_DRIVER
#define _TFT_DRV Arduino_ST7735
#define _TFT_DRVF(a, b, c, d, e, f, g, h, i, j) Arduino_ST7735(a, b, c, d, e, f, g, h, i, j)
#elif ILI9341_DRIVER
#define _TFT_DRV Arduino_ILI9341
#define _TFT_DRVF(a, b, c, d, e, f, g, h, i, j) Arduino_ILI9341(a, b, c, d, e, f)
#elif ILI9488_DRIVER
#define _TFT_DRV Arduino_ILI9488
#define _TFT_DRVF(a, b, c, d, e, f, g, h, i, j) Arduino_ILI9488(a, b, c)
#elif ILI9342_DRIVER
#define _TFT_DRV Arduino_ILI9342
#define _TFT_DRVF(a, b, c, d, e, f, g, h, i, j) Arduino_ILI9342(a, b, c, d)
#elif ST7796_DRIVER
#define _TFT_DRV Arduino_ST7796
#define _TFT_DRVF(a, b, c, d, e, f, g, h, i, j) Arduino_ST7796(a, b, c, d, e, f, g, h, i, j)
#elif RGB_PANEL
#define _TFT_DRV Arduino_RGB_Display
#define _TFT_DRVF(a, b, c, d, e, f, g, h, i, j) Arduino_RGB_Display(e, f, a, 0, true)
#elif AXS15231B_QSPI
#define _TFT_DRV Arduino_AXS15231B
#define _TFT_DRVF(a, b, c, d, e, f, g, h, i, j) Arduino_AXS15231B(a, b, c, d, e, f)
#elif DRIVER_CO5300
#define _TFT_DRV Arduino_CO5300
#define _TFT_DRVF(a, b, c, d, e, f, g, h, i, j) Arduino_CO5300(a, b, c, e, f, g, h, i, j)
#elif DRIVER_RM67162
#define _TFT_DRV Arduino_RM67162
#define _TFT_DRVF(a, b, c, d, e, f, g, h, i, j) Arduino_RM67162(a, b, c, d)
#else
// CYD Default to not shoot errors on screen
#define _TFT_DRV Arduino_ILI9341
#define _TFT_DRVF(a, b, c, d, e, f, g, h, i, j) Arduino_ILI9341(a, b, c, d, e, f)
#endif

// #define USE_CANVAS // testing purpose
#ifdef USE_CANVAS
#include <canvas/Arduino_Canvas.h>
#define ARD_TFT_BASE Arduino_Canvas
#else
#define ARD_TFT_BASE _TFT_DRV
#endif

class Ard_eSPI : public ARD_TFT_BASE {
public:
    // Driver initilizer
    Ard_eSPI(
        TFT_BUS_TYPE *bus, int8_t rst, uint8_t rotation, bool ips, uint16_t w, uint16_t h, uint16_t co1,
        uint16_t ro1, uint16_t co2, uint16_t ro2
    )
#ifdef USE_CANVAS
        : ARD_TFT_BASE(rotation & 0x1 ? h : w, rotation & 0x1 ? w : h, nullptr, 0, 0, 0),
          _outputDriver(_TFT_DRVF(bus, rst, rotation, ips, w, h, co1, ro1, co2, ro2)) {
        this->_output = &_outputDriver;
    }
#else
        : _TFT_DRVF(bus, rst, rotation, ips, w, h, co1, ro1, co2, ro2) {
    }
#endif

    inline void drawChar2(int16_t x, int16_t y, char c, int16_t a, int16_t b) { drawChar(x, y, c, a, b); };
    void drawString(String s, uint16_t x, uint16_t y);
    void drawCentreString(String s, uint16_t x, uint16_t y, int f);
    void drawRightString(String s, uint16_t x, uint16_t y, int f);
    inline int getTextsize() { return textsize_x; };
    inline uint16_t getTextcolor() { return textcolor; };
    inline uint16_t getTextbgcolor() { return textbgcolor; };
#ifdef USE_CANVAS
    inline bool begin(int32_t speed = GFX_NOT_DEFINED) { return ARD_TFT_BASE::begin(speed); }
    inline void setRotation(uint8_t rot) {
        // ARD_TFT_BASE::setRotation(rot);
        _outputDriver.setRotation(rot);
    }
    inline void invertDisplay(bool i) { _outputDriver.invertDisplay(i); }
#endif

    void display(bool a = false) {
#ifdef USE_CANVAS
        this->flush(true);
#endif
    };

private:
#ifdef USE_CANVAS
    _TFT_DRV _outputDriver;
#endif
};

#define BLACK RGB565_BLACK
#define NAVY RGB565_NAVY
#define DARKGREEN RGB565_DARKGREEN
#define DARKCYAN RGB565_DARKCYAN
#define MAROON RGB565_MAROON
#define PURPLE RGB565_PURPLE
#define OLIVE RGB565_OLIVE
#define LIGHTGREY RGB565_LIGHTGREY
#define DARKGREY RGB565_DARKGREY
#define BLUE RGB565_BLUE
#define GREEN RGB565_GREEN
#define CYAN RGB565_CYAN
#define RED RGB565_RED
#define MAGENTA RGB565_MAGENTA
#define YELLOW RGB565_YELLOW
#define WHITE RGB565_WHITE
#define ORANGE RGB565_ORANGE
#define GREENYELLOW RGB565_GREENYELLOW
#define PALERED RGB565_PALERED

#endif
#endif //__TFT_H
