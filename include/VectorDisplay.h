#pragma once
#ifndef _VECTOR_DISPLAY_H
#define _VECTOR_DISPLAY_H

#ifndef ARDUINO
#define NO_SERIAL
#include <stdint.h>
#define pgm_read_byte_near(a) (*(uint8_t*)(a))

#include <sys/timeb.h>
#include <string>

typedef std::string String;

uint32_t millis() {
    struct timeb t;
    ftime(&t);
    return t.millitm + t.time * 1000;
}

class Print {
public:    
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* s, size_t length) {
        size_t wrote = 0;
        while (length > 0) {
            size_t b = write(*s++);
            if (b <= 0)
                break;
            b++;
            length--;
        }
        return wrote;
    }
    
    virtual size_t write(const char* s) {
        return write((uint8_t*)s, strlen(s));
    }
};

class Stream : public Print {
public:    
    virtual int read() = 0;
    virtual int available() = 0;
};

#else
#include <Arduino.h>
#endif

#ifdef ESP8266
# include <ESP8266WiFi.h>
#endif

#define VECTOR_DISPLAY_MESSAGE_SIZE 8
#define VECTOR_DISPLAY_MAX_STRING 256

#define VECTOR_DISPLAY_DEFAULT_WIDTH  240
#define VECTOR_DISPLAY_DEFAULT_HEIGHT 320

#define ALIGN_LEFT 'l'
#define ALIGN_RIGHT 'r'
#define ALIGN_CENTER 'c'
#define ALIGN_TOP 't'
#define ALIGN_BOTTOM 'b'
#define ALIGN_BASELINE 'l'

#ifndef VECTOR_DISPLAY_SEND_DELAY
#define VECTOR_DISPLAY_SEND_DELAY 0
#endif

#define BLACK       0x0000      /*   0,   0,   0 */
#define NAVY        0x000F      /*   0,   0, 128 */
#define DARKGREEN   0x03E0      /*   0, 128,   0 */
#define DARKCYAN    0x03EF      /*   0, 128, 128 */
#define MAROON      0x7800      /* 128,   0,   0 */
#define PURPLE      0x780F      /* 128,   0, 128 */
#define OLIVE       0x7BE0      /* 128, 128,   0 */
#define LIGHTGREY   0xC618      /* 192, 192, 192 */
#define DARKGREY    0x7BEF      /* 128, 128, 128 */
#define BLUE        0x001F      /*   0,   0, 255 */
#define GREEN       0x07E0      /*   0, 255,   0 */
#define CYAN        0x07FF      /*   0, 255, 255 */
#define RED         0xF800      /* 255,   0,   0 */
#define MAGENTA     0xF81F      /* 255,   0, 255 */
#define YELLOW      0xFFE0      /* 255, 255,   0 */
#define WHITE       0xFFFF      /* 255, 255, 255 */
#define ORANGE      0xFD20      /* 255, 165,   0 */
#define GREENYELLOW 0xAFE5      /* 173, 255,  47 */
#define PINK        0xF81F

// Color definitions for backwards compatibility
#define ILI9341_BLACK       0x0000      /*   0,   0,   0 */
#define ILI9341_NAVY        0x000F      /*   0,   0, 128 */
#define ILI9341_DARKGREEN   0x03E0      /*   0, 128,   0 */
#define ILI9341_DARKCYAN    0x03EF      /*   0, 128, 128 */
#define ILI9341_MAROON      0x7800      /* 128,   0,   0 */
#define ILI9341_PURPLE      0x780F      /* 128,   0, 128 */
#define ILI9341_OLIVE       0x7BE0      /* 128, 128,   0 */
#define ILI9341_LIGHTGREY   0xC618      /* 192, 192, 192 */
#define ILI9341_DARKGREY    0x7BEF      /* 128, 128, 128 */
#define ILI9341_BLUE        0x001F      /*   0,   0, 255 */
#define ILI9341_GREEN       0x07E0      /*   0, 255,   0 */
#define ILI9341_CYAN        0x07FF      /*   0, 255, 255 */
#define ILI9341_RED         0xF800      /* 255,   0,   0 */
#define ILI9341_MAGENTA     0xF81F      /* 255,   0, 255 */
#define ILI9341_YELLOW      0xFFE0      /* 255, 255,   0 */
#define ILI9341_WHITE       0xFFFF      /* 255, 255, 255 */
#define ILI9341_ORANGE      0xFD20      /* 255, 165,   0 */
#define ILI9341_GREENYELLOW 0xAFE5      /* 173, 255,  47 */
#define ILI9341_PINK        0xF81F

#define MESSAGE_DOWN   'D'
#define MESSAGE_UP     'U'
#define MESSAGE_MOVE   'M'
#define MESSAGE_BUTTON 'B'
#define MESSAGE_ACK    'A'

//These enumerate the text plotting alignment (reference datum point)
#define TL_DATUM 0 // Top left (default)
#define TC_DATUM 1 // Top centre
#define TR_DATUM 2 // Top right
#define ML_DATUM 3 // Middle left
#define CL_DATUM 3 // Centre left, same as above
#define MC_DATUM 4 // Middle centre
#define CC_DATUM 4 // Centre centre, same as above
#define MR_DATUM 5 // Middle right
#define CR_DATUM 5 // Centre right, same as above
#define BL_DATUM 6 // Bottom left
#define BC_DATUM 7 // Bottom centre
#define BR_DATUM 8 // Bottom right
#define L_BASELINE  9 // Left character baseline (Line the 'A' character would sit on)
#define C_BASELINE 10 // Centre character baseline
#define R_BASELINE 11 // Right character baseline

typedef uint32_t FixedPoint32;
#define TO_FP32(f) ((uint32_t)((f)*65536. + 0.5))

struct VectorDisplayMessage {
    char what;
    char what2;
    union {
        uint8_t button;
        struct {
            int16_t x;
            int16_t y;
        } xy;
    } data;
} __attribute__((packed));

class VectorDisplayClass : public Print {
private:
    static const uint32_t MAX_BUFFER = (uint32_t)1024*256;
    static const uint32_t MESSAGE_TIMEOUT = 3000;
    static const uint8_t FLAG_LOW_ENDIAN_BITS = 1;
    static const uint8_t FLAG_HAVE_MASK = 2;
    static const uint8_t FLAG_PAD_BYTE = 4;
    static const uint8_t FLAG_LOW_ENDIAN_BYTES = 8;

    bool waitForAck = true;
    int gfxFontSize = 1;
    int curx = 0;
    int cury = 0;
    int readPos = 0;
    int32_t curForeColor565 = -1;
    uint32_t lastMessageStart = 0;
    int pointerX;
    int pointerY;
    int curWidth = VECTOR_DISPLAY_DEFAULT_WIDTH;
    int curHeight = VECTOR_DISPLAY_DEFAULT_HEIGHT;
    uint8_t curRotation = 0;
    bool pointerDown = false;
    bool wrap = 1;
    bool fixCP437 = true;
    uint16_t polyLineCount;
    uint8_t polyLineSum;
    uint32_t delayTime = 0;
    
    uint8_t readBuf[VECTOR_DISPLAY_MESSAGE_SIZE];
    union {
        uint32_t color;
        uint16_t twoByte[9];
        struct {
            uint16_t x;
            uint16_t y;
            char text[VECTOR_DISPLAY_MAX_STRING+1];
        } __attribute__((packed)) xyText;
        struct {
            uint16_t endianness;
            uint16_t width;
            uint16_t height;
            FixedPoint32 aspectRatio;
            uint16_t reserved[3];
        } __attribute__((packed)) initialize;
        struct {
            uint8_t c;
            char text[VECTOR_DISPLAY_MAX_STRING+1];
        } __attribute__((packed)) charText;
        struct {
            uint16_t width;
            uint16_t height;
        } __attribute__((packed)) coords;
        struct {
            char attr;
            uint8_t value;
        } __attribute__((packed)) attribute8;
        struct {
            char attr;
            uint16_t value;
        } __attribute__((packed)) attribute16;
        struct {
            char attr;
            uint32_t value;
        } __attribute__((packed)) attribute32;
        struct {
            uint32_t length;
            uint8_t depth;
            uint8_t flags;
            uint16_t x;
            uint16_t y;
            uint16_t w;
            uint16_t h;
            uint32_t foreColor; // only if depth==1
            uint32_t backColor; // only if depth==1
        } __attribute__((packed)) bitmap;
        struct {
            uint16_t x1;
            uint16_t y1;
            uint16_t x2;
            uint16_t y2;
            uint16_t r;
            uint8_t filled;
        } __attribute__((packed)) roundedRectangle;
        struct {
            uint16_t x;
            uint16_t y;
            uint16_t r;
            FixedPoint32 angle1;
            FixedPoint32 sweep;
            uint8_t filled;
        } __attribute__((packed)) arc;
        struct {
            char attr;
            uint16_t values[2];
        } __attribute__((packed)) attribute16x2;        
        uint8_t bytes[VECTOR_DISPLAY_MAX_STRING+1];
        char text[VECTOR_DISPLAY_MAX_STRING+1];
    } args;
    uint32_t lastSend = 0;
    
private:    
    inline void sendDelay() {
        if (delayTime>0) {
            while(millis()-lastSend < delayTime) ;
            lastSend = millis();
        }
    }
    
public:    
    int textsize = 1;
    uint32_t textcolor = WHITE;
    uint32_t textbgcolor = BLACK;

    void setWaitForAck(bool wait) {
        waitForAck = wait;
    }

    void setDelay(uint32_t delayMillis) {
        delayTime = delayMillis;
        lastSend = millis();
    }
    
    virtual void remoteFlush() {
        /*while(remoteAvailable()) 
            remoteRead();
            * */
    }
    virtual int remoteRead() = 0; // must be non-blocking
    virtual void remoteWrite(uint8_t c) = 0;
    virtual void remoteWrite(const void* data, size_t n) = 0;
    virtual size_t remoteAvailable() = 0;
    
    void attribute8(char a, uint8_t value) {
    }

    void attribute8(char a, bool value) {
    }

    void attribute16(char a, uint16_t value) {
    }

    void attribute32(char a, uint32_t value) {
    }

    void sendCommand(char c, const void* arguments, int argumentsLength) {
    }
   
    
    void sendCommandWithAck(char c, const void* arguments, int argumentsLength) {   
    }    
    
    uint16_t width() {
        return (curRotation%2)?curHeight:curWidth;
    }
    
    uint16_t height() {
        return (curRotation%2)?curWidth:curHeight;
    }

    uint8_t sumBytes(void* data, int length) {
        uint8_t* p = (uint8_t*)data;
        uint8_t s = 0;
        while(length-- > 0)
            s += *p++;
        return s;
    }
    void display(bool a = false) {
    }

    void startPoly(char c, uint16_t n) {
    }

    void startFillPoly(uint16_t n) {
    }

    void startPolyLine(uint16_t n) {
    }

    void addPolyLine(int16_t x, int16_t y) {
    }

    void line(int x1, int y1, int x2, int y2) {
    }
    
    void fillRectangle(int x1, int y1, int x2, int y2) {
    }
    
    void rectangle(int x1, int y1, int x2, int y2, bool fill=false) {
    }
    
    void roundedRectangle(int x1, int y1, int x2, int y2, int r, bool fill) {
    }
    
    void roundedRectangle(int x1, int y1, int x2, int y2, int r) {
    }
    
    void fillRoundedRectangle(int x1, int y1, int x2, int y2, int r) {
    }
    
    void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3) {
    }
    
/*    void initialize() {
        args.twoByte[0] = 0x1234; // endianness detector
        args.twoByte[1] = 0;
        sendCommandWithAck('H', &args, 4);
    } */
    
    void initialize(int w=VECTOR_DISPLAY_DEFAULT_WIDTH, int h=VECTOR_DISPLAY_DEFAULT_HEIGHT) {
    }
    
    void fillCircle(int x, int y, int r) {
    }
    
    void circle(int x, int y, int r) {
    }
    
    void point(int x, int y) {
    }
    
    void arc(int x, int y, int r, FixedPoint32 angle1, FixedPoint32 sweep, bool fill=false) {
    }
    
    void arc(int x, int y, int r, float angle1, float sweep, bool fill=false) {
    }
    
    // 32-bit fixed point
    void textSize(FixedPoint32 s) {
    }
    
    void text(int x, int y, const char* str, int n) {
    }
    
    void text(int x, int y, const char* str) {
    }
    
    void text(int x, int y, String str) {
    }
    
    void deleteButton(uint8_t command) {
    }

    void addButton(uint8_t command, const char* str) {
    }

    void addButton(uint8_t command, String str) {
    }

    void toast(const char* str, unsigned n) {
    }
    
    void toast(const char* str) {
    }
    
    void toast(String text) {
    }
    
    void foreColor(uint32_t color) {
    }

    void backColor(uint32_t color) {
    }

    void textBackColor(uint32_t color) {
    }
    
    void textForeColor(uint32_t color) {
    }
    
    void foreColor565(uint16_t color) {
    }

    void backColor565(uint16_t color) {
    }

    void textBackColor565(uint16_t color) {
    }
    
    void textForeColor565(uint16_t color) {
    }
    
    void rounded(uint8_t value) {
    }
    
    void thickness(FixedPoint32 t) {
    }

    void pixelAspectRatio(FixedPoint32 a) {
    }

#ifdef SUPPORT_FLOATING_POINT
    inline void setThickness(double thickness) {
        setThickness(TO_FP32(thickness));
    } 

    inline void setPixelAspectRatio(double aspect) {
        setThickness(TO_FP32(aspect));
    } 
#endif    

    void clear() {
        sendCommand('C', NULL, 0);
    }

    void update() {
        sendCommand('F', NULL, 0);
    }

/*    void reset() {
        sendCommandWithAck('E', NULL, 0);
    } */

    void coordinates(int width, int height) {
    }
    
    void continuousUpdate(bool value) {
    }
    
    void textHorizontalAlign(char hAlign) {
    }

    void textVerticalAlign(char hAlign) {
    }

    void textOpaqueBackground(bool opaque) {
    }
    
    void textBold(bool bold) {
    }
    
    bool isTouchDown() {
    }
    
    int getTouchX() {
    }
    
    int getTouchY() {
    }
        
    bool readMessage(VectorDisplayMessage* msg) {
    }
    
    uint32_t color565To8888(uint16_t c) {
    }
    
    uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    }
    
    uint32_t getBitmap1Size(int16_t w, int16_t h, uint8_t flags=0) {
    }
    
    uint32_t getBitmapSize(int16_t w, int16_t h, uint8_t depth=1, uint8_t flags=0) {

    }
    
    /*TODO: stubs*/
    void*    createSprite(int16_t width, int16_t height, uint8_t frames = 1) { return NULL; }
    void     pushSprite(int32_t x, int32_t y) {}
    void     deleteSprite(void) {}
    void     fillSprite(uint32_t color) {}

    void bitmap_progmem(int16_t x, int16_t y, const uint8_t* bmp,
      int16_t w, int16_t h, uint8_t depth=1, uint8_t flags=0, const uint8_t* mask=NULL, 
      uint32_t foreColor=0xFFFFFFFF, 
      uint32_t backColor=0x00FFFFFF) /* PROGMEM */ {
    }

    void bitmap(int16_t x, int16_t y, uint8_t *bmp,
      int16_t w, int16_t h, uint8_t depth, uint8_t flags=0, 
      uint8_t* mask=NULL,
      uint32_t foreColor=0xFFFFFFFF, 
      uint32_t backColor=0x00FFFFFF) {
    }
    
    void utf8() {
    }

    /* The following are meant to be compatible with Adafruit GFX */
    void cp437(bool s) {    }
    
    void setRotation(uint8_t r) {    }
    
    void setTextSize(uint8_t size) {    }

    void setTextDatum(uint8_t d) { }  // mockup
        
    void setTextColor(uint16_t f, uint16_t b) {    }
    
    void setTextColor(uint16_t f) {    }
    
    void setCursor(int16_t x, int16_t y) {    }
    
    int getTextsize() { return 0; }
    uint16_t getTextcolor() { return 0; }
    uint16_t getTextbgcolor() { return 0; }

  int16_t  getCursorX(void) {  }
  
  int16_t  getCursorY(void) { }
    
    void setTextWrap(bool w) {    }

    int16_t drawRightString(const char *string, int32_t x, int32_t y, uint8_t font) {    }
    
    int16_t drawRightString(const String& string, int32_t x, int32_t y, uint8_t font) {    }
    
    int16_t drawCentreString(const char *string, int32_t x, int32_t y, uint8_t font) {    }
    
    
    int16_t drawCentreString(const String& string, int32_t x, int32_t y, uint8_t font) {    }
    
    int16_t drawString(const String& string, int32_t x, int32_t y) {    }
    
    int16_t drawString(const char *string, int32_t x, int32_t y) {    };

    int16_t  drawChar2(uint16_t uniCode, int32_t x, int32_t y, uint16_t a, uint16_t b) {    }
                   
        // TODO: fix back color handling
    size_t write(uint8_t c) override {    }

        // TODO: fix back color handling
    size_t write(const char* s) /*override*/ { //ESP8266 core doesn't supply write(const char*)
            }
    
    void drawPixel(int16_t x, int16_t y, uint16_t color) {    }

    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {    }
    
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {    }
    
    void drawLine(int16_t x, int16_t y, int16_t x2, int16_t y2, uint16_t color) {    }
    
    
    // Draw an anti-aliased wide line from ax,ay to bx,by width wd with radiused ends (radius is wd/2)
    // If bg_color is not included the background pixel colour will be read from TFT or sprite
    void  drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF) {    }

   // As per "drawSmoothArc" except the ends of the arc are NOT anti-aliased, this facilitates dynamic arc length changes with
   // arc segments and ensures clean segment joints.
   // The sides of the arc are anti-aliased by default. If smoothArc is false sides will NOT be anti-aliased
    void drawArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle, uint32_t fg_color, uint32_t bg_color = 0, bool smoothArc = true){    }

    void drawSmoothArc(int32_t x, int32_t y, int32_t r, int32_t ir, uint32_t startAngle, uint32_t endAngle, uint32_t fg_color, uint32_t bg_color = 0, bool roundEnds = false) {    }

    // Draw an anti-aliased filled circle at x, y with radius r
    // If bg_color is not included the background pixel colour will be read from TFT or sprite
    void fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color, uint32_t bg_color = 0x00FFFFFF) {    }

  void   drawRoundRect(int32_t x, int32_t y, int32_t r, int32_t ir, int32_t w, int32_t h, uint32_t fg_color, uint32_t bg_color = 0x00FFFFFF, uint8_t quadrants = 0xF) {  }

  // Draw a filled rounded rectangle , corner radius r and bounding box defined by x,y and w,h
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color, uint32_t bg_color) {  }


    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {    }
    
    void fillScreen(uint16_t color) {    }
    
    void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {    }

    void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {    }
    
    void drawEllipse(int16_t x, int16_t y, int32_t rx, int32_t ry, uint16_t color) {        }
    void fillEllipse(int16_t x, int16_t y, int32_t rx, int32_t ry, uint16_t color) {    }
    
    virtual void begin(int width=VECTOR_DISPLAY_DEFAULT_WIDTH, int height=VECTOR_DISPLAY_DEFAULT_HEIGHT) {    }
    
    virtual void end() {
    }
    
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
      int16_t x2, int16_t y2, uint16_t color) {
    }

    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
      int16_t x2, int16_t y2, uint16_t color) {    }

    void drawRoundRect(int16_t x0, int16_t y0, int16_t w, int16_t h,
      int16_t radius, uint16_t  color) {    }
      
    void fillRoundRect(int16_t x0, int16_t y0, int16_t w, int16_t h,
      int16_t radius, uint16_t color) {
    }

    void drawBitmap(int16_t x, int16_t y, const uint8_t bmp[],
      int16_t w, int16_t h, uint16_t color) /* PROGMEM */ {    }

    void drawBitmap(int16_t x, int16_t y, uint8_t *bmp,
      int16_t w, int16_t h, uint16_t color) {    }

    void drawBitmap(int16_t x, int16_t y, const uint8_t bmp[],
      int16_t w, int16_t h, uint16_t color, uint16_t bg) {    }
    
    void drawBitmap(int16_t x, int16_t y, uint8_t *bmp,
      int16_t w, int16_t h, uint16_t color, uint16_t bg) {    }

    void drawXBitmap(int16_t x, int16_t y, const uint8_t bmp[],
      int16_t w, int16_t h, uint16_t color) {     
    }
    
    void drawXBitmap(int16_t x, int16_t y, const uint8_t bmp[],
      int16_t w, int16_t h, uint16_t color, uint16_t bgcolor) {    }

    void drawGrayscaleBitmap(int16_t x, int16_t y, const uint8_t bmp[],
      int16_t w, int16_t h) {    }
    
    void drawGrayscaleBitmap(int16_t x, int16_t y, uint8_t *bmp,
      int16_t w, int16_t h) {    }

    void drawGrayscaleBitmap(int16_t x, int16_t y,
      const uint8_t bmp[], const uint8_t mask[],
      int16_t w, int16_t h) {    }
    
    void drawGrayscaleBitmap(int16_t x, int16_t y,
      uint8_t *bmp, uint8_t *mask, int16_t w, int16_t h) {    }

    void drawRGBBitmap(int16_t x, int16_t y, uint16_t* bmp,
      int16_t w, int16_t h) {                
    }

    void drawRGBBitmap(int16_t x, int16_t y, const uint16_t bmp[],
      int16_t w, int16_t h) {                           
    }

    void drawRGBBitmap(int16_t x, int16_t y,
      const uint16_t bmp[], const uint8_t mask[],
      int16_t w, int16_t h) {                              
    }
      
    void drawRGBBitmap(int16_t x, int16_t y,
      uint16_t *bmp, uint8_t *mask, int16_t w, int16_t h) {
    }

    void drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername,
      uint16_t color) {
          
      }

    void fillCircleHelper(int16_t cx, int16_t cy, int16_t r, uint8_t corners,
      int16_t delta, uint16_t color) {
      }

  void     pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data) {};
  void     pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data, uint16_t transparent) {};


      /* the following Adafruit GFX APIs are not implemented at present */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
    void drawChar2(int16_t x, int16_t y, unsigned char c, uint16_t color,
      uint16_t bg, uint8_t size) {}
    void setFont(const void /*GFXfont*/ *f = NULL) {}
    void getTextBounds(const char *string, int16_t x, int16_t y,
      int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h) {}
    void getTextBounds(const void /*__FlashStringHelper*/ *s, int16_t x, int16_t y,
      int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h) {}    
#pragma GCC diagnostic pop
};

class SerialDisplayClass : public VectorDisplayClass {
    private:
        Stream& s;
        const bool doSerialBegin;
    
    public:
        virtual int remoteRead() override {
            //return s.read();
            return('A');
        }
        
        virtual void remoteWrite(uint8_t c) override {
            //s.write(c);
        }
        
        virtual void remoteWrite(const void* data, size_t n) override {
            //s.write((uint8_t*)data, n);
        }

        /* only works with the Serial object; do not call externally without it */
        void begin(uint32_t speed, int width=VECTOR_DISPLAY_DEFAULT_WIDTH, int height=VECTOR_DISPLAY_DEFAULT_HEIGHT) {
#ifndef NO_SERIAL
            if (doSerialBegin) {
                Serial.begin(speed);
                while(!Serial) ;
            }
#endif
            VectorDisplayClass::begin(width, height);
        }
        
        bool getSwapBytes(void) { return false; }  // stub
        void setSwapBytes(bool swap) { return; }  // stub
        
        virtual void begin(int width=VECTOR_DISPLAY_DEFAULT_WIDTH, int height=VECTOR_DISPLAY_DEFAULT_HEIGHT) override {
            begin(115200, width, height);
        }
        
        virtual size_t remoteAvailable() override {
            return s.available();
        }        
        
#ifndef NO_SERIAL
        SerialDisplayClass() : s(Serial), doSerialBegin(true)  {}
#endif

        SerialDisplayClass(Stream& _s) : s(_s), doSerialBegin(false) {}
};

#ifdef ESP8266
class WiFiDisplayClass : public SerialDisplayClass {
    private:
        WiFiClient client;
    public:    
        bool begin(const char* host, int width=VECTOR_DISPLAY_DEFAULT_WIDTH, int height=VECTOR_DISPLAY_DEFAULT_HEIGHT) {
            VectorDisplayClass::begin(width, height);
            return client.connect(host, 7788);
        }
        
        virtual void end() override {
            VectorDisplayClass::end();
            client.stop();
        }
        
        WiFiDisplayClass() : SerialDisplayClass(client) {            
        }
};
#endif
        
#endif
