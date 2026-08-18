#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "nvs_helpers.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

// Elecrow's own docs disagree with themselves on the SDIO pinout of the
// onboard ESP32-C6 co-processor between hardware revisions (the schematic's
// D0-D3 order didn't work on the unit this was tested on; the reverse order,
// now the build default, does -- confirmed working with the co-processor's
// ESP-Hosted firmware: Wi-Fi scan and AP connect both succeed). Rather than
// require a rebuild+reflash if a different revision needs the other order,
// every pin can still be overridden at runtime from NVS (see the "sdio"
// serial console command in serial_console.cpp), falling back to the
// build-time SDIO2_* macros when no override is stored.
static int8_t sdioPinOverride(const char *key, int8_t buildDefault) {
    lnvs::Handle h(SDIO_OVERRIDE_NVS_NS, false);
    if (!h) return buildDefault;
    int value;
    if (!lnvs::getInt(h.raw(), key, value)) return buildDefault;
    return (int8_t)value;
}

#define TOUCH_MODULES_GT911
#define TOUCH_SDA_PIN GT911_I2C_CONFIG_SDA_IO_NUM
#define TOUCH_SCL_PIN GT911_I2C_CONFIG_SCL_IO_NUM
#define TOUCH_RST_PIN GT911_TOUCH_CONFIG_RST_GPIO_NUM
#define TOUCH_ADDR GT911_SLAVE_ADDRESS1

#include <TouchLib.h>

class ElecrowTouch : public TouchLib {
public:
    LTouchPoint t;
    TP_Point ti;
    ElecrowTouch() : TouchLib(Wire, TOUCH_SDA_PIN, TOUCH_SCL_PIN, TOUCH_ADDR, TOUCH_RST_PIN) {}
    inline bool begin() {
        bool result = init();
        setRotation(ROTATION);
        return result;
    }
    inline bool touched() { return read(); }
};
ElecrowTouch touch;

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
    // LCD_BK_POWER: P-MOS load switch feeding the backlight boost converter's
    // VIN. Active LOW. Must be on before the boost EN (TFT_BL) does anything.
    pinMode(TFT_BL_POWER, OUTPUT);
    digitalWrite(TFT_BL_POWER, LOW);
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

    if (!touch.begin()) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());

    // ESP32-P4 has no native radio; WiFi/BT come from the onboard ESP32-C6 over
    // SDIO. Pins are schematic-derived, not confirmed on hardware -- resolve
    // each one through an NVS override first (see "sdio" serial command) so a
    // different guess can be tried without rebuilding.
    int8_t sdioClk = sdioPinOverride("clk", SDIO2_CLK);
    int8_t sdioCmd = sdioPinOverride("cmd", SDIO2_CMD);
    int8_t sdioD0 = sdioPinOverride("d0", SDIO2_D0);
    int8_t sdioD1 = sdioPinOverride("d1", SDIO2_D1);
    int8_t sdioD2 = sdioPinOverride("d2", SDIO2_D2);
    int8_t sdioD3 = sdioPinOverride("d3", SDIO2_D3);
    int8_t sdioRst = sdioPinOverride("rst", SDIO2_RST);
    launcherConsolePrintf(
        "SDIO pins: clk=%d cmd=%d d0=%d d1=%d d2=%d d3=%d rst=%d\n",
        sdioClk,
        sdioCmd,
        sdioD0,
        sdioD1,
        sdioD2,
        sdioD3,
        sdioRst
    );
    if (!launcherWifiInitSdioAuto(sdioClk, sdioCmd, sdioD0, sdioD1, sdioD2, sdioD3, sdioRst)) {
        launcherConsolePrintln("WIFI unavailable");
    }
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

    log_i("dutyCycle for bright 0-255: %d", dutyCycle);
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
    bool touched = touch.touched(); // read every cycle to skip bad readings
    if (launcherMillis() - d_tmp > 250 || LongPress) {
        if (touched) {
            auto t = touch.getPoint(0);
            launcherConsolePrintf(
                "\nTouch Pressed on x=%d, y=%d, rot: %d, width=%d, height=%d",
                t.x,
                t.y,
                rotation,
                displayConfig.width,
                displayConfig.height
            );
            d_tmp = launcherMillis();

            if (rotation == 0) {
                uint16_t tmp = t.x;
                t.x = t.y;
                t.y = tmp;
            }

            if (rotation == 1) { t.y = displayConfig.width - t.y; }

            if (rotation == 2) {
                uint16_t tmp = t.x;
                t.x = displayConfig.width - t.y;
                t.y = displayConfig.height - tmp;
            }
            if (rotation == 3) { t.x = displayConfig.height - t.x; }

            launcherConsolePrintf("\nAfterPressed on x=%d, y=%d, rot: %d\n", t.x, t.y, rotation);

            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        }
    } else touch.touched(); // keep calling it to keep refreshing raw readings for when it's needed
}
