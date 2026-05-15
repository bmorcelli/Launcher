#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

#ifndef TFT_BRIGHT_CHANNEL
#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_FREQ 5000
#define TFT_BRIGHT_Bits 8
#ifndef TFT_BL
#define TFT_BL GPIO_BCKL
#endif
#endif

#if defined(HAS_CAPACITIVE_TOUCH)
#include "CYD28_TouchscreenC.h"
#define CYD28_DISPLAY_HOR_RES_MAX 240
#define CYD28_DISPLAY_VER_RES_MAX 320
CYD28_TouchC touch(CYD28_DISPLAY_HOR_RES_MAX, CYD28_DISPLAY_VER_RES_MAX);

#elif defined(TOUCH_GT911_I2C) || defined(TOUCH_CST816S_I2C)
#ifdef TOUCH_GT911_I2C
#define TOUCH_MODULES_GT911
#define TOUCH_SDA_PIN GT911_I2C_CONFIG_SDA_IO_NUM
#define TOUCH_SCL_PIN GT911_I2C_CONFIG_SCL_IO_NUM
#define TOUCH_RST_PIN GT911_TOUCH_CONFIG_RST_GPIO_NUM
#define TOUCH_INT_PIN GT911_TOUCH_CONFIG_INT_GPIO_NUM
#define TOUCH_ADDR GT911_SLAVE_ADDRESS1
#ifndef TOUCH_INVERTED
#define TOUCH_INVERTED 0
#endif
#elif TOUCH_CST816S_I2C
#define TOUCH_MODULES_CST_SELF
#define TOUCH_SDA_PIN CST816S_I2C_CONFIG_SDA_IO_NUM
#define TOUCH_SCL_PIN CST816S_I2C_CONFIG_SCL_IO_NUM
#define TOUCH_RST_PIN CST816S_TOUCH_CONFIG_RST_GPIO_NUM
#define TOUCH_INT_PIN CST816S_TOUCH_CONFIG_INT_GPIO_NUM
#define TOUCH_ADDR CTS820_SLAVE_ADDRESS
#endif

#include <TouchLib.h>
class CYD_Touch : public TouchLib {
public:
    LTouchPoint t;
    TP_Point ti;
    CYD_Touch() : TouchLib(Wire, TOUCH_SDA_PIN, TOUCH_SCL_PIN, TOUCH_ADDR, TOUCH_RST_PIN) {}
    inline bool begin() {
        bool result = init();
        setRotation(ROTATION);
        return result;
    }
    inline bool touched() { return read(); }
    inline LTouchPoint getPointScaled() {
        ti = getPoint(0);
#if TOUCH_INVERTED
        t.x = ti.y;
        t.y = TFT_WIDTH - ti.x;
#else
        t.x = ti.x;
        t.y = (tftHeight + 20) - ti.y;
#endif
        t.pressed = true;
        TouchLib::raw_data[0] = 0; // resets the read raw reading, that triggers TouchLib::read() to true, and
                                   // is not resetted at the lib
        return t;
    }
};
CYD_Touch touch;

#elif defined(TOUCH_AXS15231B_I2C)
#include <bb_captouch.h>
#define TOUCH_SDA_PIN AXS15231B_TOUCH_I2C_SDA
#define TOUCH_SCL_PIN AXS15231B_TOUCH_I2C_SCL
#define TOUCH_RST_PIN AXS15231B_TOUCH_I2C_RST
#define TOUCH_INT_PIN AXS15231B_TOUCH_I2C_IRQ

class CYD_Touch : public BBCapTouch {
public:
    LTouchPoint t;
    TOUCHINFO ti;
    CYD_Touch() : BBCapTouch() {}
    inline bool begin() {
        const char *szNames[] = {"Unknown", "FT6x36", "GT911", "CST820", "CST226", "MXT144", "AXS15231"};
        Wire.end();
        launcherConsolePrintf("%s\n", String("Starting Touch Sensor").c_str());
        bool result =
            init(TOUCH_SDA_PIN, TOUCH_SCL_PIN, TOUCH_RST_PIN, TOUCH_INT_PIN); // returns 0 if CT_SUCCESS;
        setOrientation(
            90, TFT_WIDTH, TFT_HEIGHT
        ); // This orientation reflects the right position for the InputHandler logic.
        int iType = sensorType();
        launcherConsolePrintf("Sensor type = %s\n", szNames[iType]);
        return result == 0 ? true : false;
    }
    inline bool touched() {
        if (getSamples(&ti)) {
            t.x = ti.x[0];
            t.y = ti.y[0];
            t.pressed = true;
        } else {
            t.x = 0;
            t.y = 0;
            t.pressed = false;
        }
        return t.pressed;
    }
    inline LTouchPoint getPointScaled() { return t; }
};
CYD_Touch touch;

#else
#include "CYD28_TouchscreenR.h"
#ifndef CYD28_DISPLAY_HOR_RES_MAX
#define CYD28_DISPLAY_HOR_RES_MAX 320
#endif

#ifndef CYD28_DISPLAY_VER_RES_MAX
#define CYD28_DISPLAY_VER_RES_MAX 240
#endif
CYD28_TouchR touch(CYD28_DISPLAY_HOR_RES_MAX, CYD28_DISPLAY_VER_RES_MAX);
#endif

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
#if !defined(HAS_CAPACITIVE_TOUCH) &&                                                                        \
    (defined(TOUCH_GT911_I2C) || defined(TOUCH_CST816S_I2C) || defined(TOUCH_AXS15231B_I2C))
    Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
#endif
#if !defined(HAS_CAPACITIVE_TOUCH) && defined(CYD)
    launcherGpioOutput(33); // CS Pin
#elif defined(CYDS3)
    launcherGpioOutput(38); // CS Pin
#endif
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // Brightness control must be initialized after tft in this case @Pirata
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);

    if (!touch.begin(
#ifdef CYD28_TouchR_MOSI
#if TFT_MOSI == CYD28_TouchR_MOSI
            &SPI
#endif
#endif

        )) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 250;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 250) / 100);

    launcherConsolePrintf("dutyCycle for bright 0-255: %d", dutyCycle);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long d_tmp = launcherMillis();
    bool touched = touch.touched();                    // read every cycle to skip bad readings
    if (launcherMillis() - d_tmp > 250 || LongPress) { // I know R3CK.. I Should NOT nest if statements..
        // but it is needed to not keep SPI bus used without need, it save resources
        LTouchPoint t;
#ifdef DONT_USE_INPUT_TASK
        checkPowerSaveTime();
#endif
        if (touched) {
            auto t = touch.getPointScaled();
#if defined(HAS_RESISTIVE_TOUCH)
            auto t2 = touch.getPointRaw();
            launcherConsolePrintf("\nRAW: Touch Pressed on x=%d, y=%d, rot: %d", t2.x, t2.y, rotation);
#endif
            launcherConsolePrintf("\nBEF: Touch Pressed on x=%d, y=%d, rot: %d", t.x, t.y, rotation);
            d_tmp = launcherMillis();
#ifdef DONT_USE_INPUT_TASK // need to reset the variables to avoid ghost click
            NextPress = false;
            PrevPress = false;
            UpPress = false;
            DownPress = false;
            SelPress = false;
            EscPress = false;
            AnyKeyPress = false;
            touchPoint.pressed = false;
#endif

            if (rotation == 3) {
                t.y = (tftHeight + 20) - t.y;
                t.x = tftWidth - t.x;
            }
            if (rotation == 0) {
                int tmp = t.x;
                t.x = tftWidth - t.y;
                t.y = tmp;
            }
            if (rotation == 2) {
                int tmp = t.x;
                t.x = t.y;
                t.y = (tftHeight + 20) - tmp;
            }
            launcherConsolePrintf("\nAFT: Touch Pressed on x=%d, y=%d, rot: %d\n", t.x, t.y, rotation);
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        }
    }
#ifdef TOUCH_GT911_I2C
    else
        touch.touched(); // keep calling it to keep refreshing raw readings for when needed it will be ok
#endif
}

void reboot() {
    // Some Marauder CYDs use GPIO 1/3 with GPS, these pins are used for USB Serial too
    // so it conflicts and as Serial is already started with launcher, we need to
    // finish this process to release the pins. Same for some Bruce mods
#if defined(CYD_RELEASE_SERIAL)
    launcherConsolePrintf("%s", String("\r\n").c_str());
    launcherConsoleFlush();
    launcherConsoleEnd();
    vTaskDelay(pdMS_TO_TICKS(50));
    launcherGpioInput(1);
    launcherGpioInput(3);
    vTaskDelay(pdMS_TO_TICKS(10));
#endif
    ESP.restart();
}
