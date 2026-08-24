#include "hal/device.h"
#include "hal/inputs/touch.h"
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

#define TOUCH_SDA_PIN GT911_I2C_CONFIG_SDA_IO_NUM
#define TOUCH_SCL_PIN GT911_I2C_CONFIG_SCL_IO_NUM
#define TOUCH_RST_PIN GT911_TOUCH_CONFIG_RST_GPIO_NUM
#define TOUCH_INT_PIN GT911_TOUCH_CONFIG_INT_GPIO_NUM
#define TOUCH_ADDR 0x5D // GT911 default I2C address

static bool touchReady = false;

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_rst = TOUCH_RST_PIN;
    cfg.pin_irq = TOUCH_INT_PIN;
    // rotation:        0      1      2      3
    bool swapXY[4] = {false, true, false, true};
    bool mirrorX[4] = {false, false, true, true};
    bool mirrorY[4] = {false, true, true, false};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

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

    touchReady = hal_touch_init(touchCfg(), TOUCH_ADDR);
    if (!touchReady) {
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
    if (launcherMillis() - d_tmp > 250 || LongPress) {
        LTouchPoint t;
        if (touchReady && hal_touch_read(touchCfg(), t)) {
            d_tmp = launcherMillis();
            if (!hal_touch_apply(t)) return;
        }
    }
}
