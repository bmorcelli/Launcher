#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "nvs_helpers.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

// ---------------------------------------------------------------------------
// HX8394 init sequence (720x1280, 2 MIPI-DSI lanes)
//
// Waveshare's official register sequence for this panel, transcribed from
// their audited BSP (waveshareteam/ESP32-P4-WIFI6-Touch-LCD-5,
// HX8394_720_1280_PANEL_30HZ_DPI_CONFIG -- the vendor_specific_init_default
// table in examples/arduino/libraries/displays/displays_config.h). The
// stock Arduino_GFX hx8394_init_operations array that TFT_DSI_INIT names in
// platformio.ini belongs to a different panel revision (different power,
// gamma, VCOM and GIP tables) and is only there to satisfy the build-time
// checks; _setup_gpio() below re-aims displayConfig at this table before
// tft->begin(), following the runtime-swap pattern boards/lilygo-t-display-p4
// uses for its AMOLED variant.
// ---------------------------------------------------------------------------
static const lcd_init_cmd_t waveshareHx8394InitOperations[] = {
    //  {cmd, { data }, data_size, delay_ms}
    // sleep out, MADCTL, 16-bit COLMOD and 2-lane DSI config, sent by the
    // BSP ahead of the vendor sequence
    {0x11, (uint8_t[]){0x00},                                                                         1,  120},
    {0x36, (uint8_t[]){0x00},                                                                         1,  0  },
    {0x3A, (uint8_t[]){0x55},                                                                         1,  0  },
    {0xBA, (uint8_t[]){0x61},                                                                         1,  0  },
    // vendor sequence
    {0xB9, (uint8_t[]){0xFF, 0x83, 0x94},                                                             3,  0  },
    {0xB1, (uint8_t[]){0x48, 0x0A, 0x6A, 0x09, 0x33, 0x54, 0x71, 0x71, 0x2E, 0x45},                   10, 0  },
    {0xBA, (uint8_t[]){0x61, 0x03, 0x68, 0x6B, 0xB2, 0xC0},                                           6,  0  },
    {0xB2, (uint8_t[]){0x00, 0x80, 0x64, 0x0C, 0x06, 0x2F},                                           6,  0  },
    {0xB4,
     (uint8_t[]){0x1C, 0x78, 0x1C, 0x78, 0x1C, 0x78, 0x01, 0x0C, 0x86, 0x75, 0x00,
                 0x3F, 0x1C, 0x78, 0x1C, 0x78, 0x1C, 0x78, 0x01, 0x0C, 0x86},
     21,                                                                                                  0  },
    {0xD3,
     (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x32, 0x10, 0x05,
                 0x00, 0x05, 0x32, 0x13, 0xC1, 0x00, 0x01, 0x32, 0x10, 0x08, 0x00,
                 0x00, 0x37, 0x03, 0x07, 0x07, 0x37, 0x05, 0x05, 0x37, 0x0C, 0x40},
     33,                                                                                                  0  },
    {0xD5,
     (uint8_t[]){0x18, 0x18, 0x18, 0x18, 0x22, 0x23, 0x20, 0x21, 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02,
                 0x03, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x19, 0x19},
     44,                                                                                                  0  },
    {0xD6,
     (uint8_t[]){0x18, 0x18, 0x19, 0x19, 0x21, 0x20, 0x23, 0x22, 0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05,
                 0x04, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x18, 0x18},
     44,                                                                                                  0  },
    {0xE0,
     (uint8_t[]){0x07, 0x08, 0x09, 0x0D, 0x10, 0x14, 0x16, 0x13, 0x24, 0x36, 0x48, 0x4A, 0x58, 0x6F, 0x76,
                 0x80, 0x97, 0xA5, 0xA8, 0xB5, 0xC6, 0x62, 0x63, 0x68, 0x6F, 0x72, 0x78, 0x7F, 0x7F, 0x00,
                 0x02, 0x08, 0x0D, 0x0C, 0x0E, 0x0F, 0x10, 0x24, 0x36, 0x48, 0x4A, 0x58, 0x6F, 0x78, 0x82,
                 0x99, 0xA4, 0xA0, 0xB1, 0xC0, 0x5E, 0x5E, 0x64, 0x6B, 0x6C, 0x73, 0x7F, 0x7F},
     58,                                                                                                  0  },
    {0xCC, (uint8_t[]){0x0B},                                                                         1,  0  },
    {0xC0, (uint8_t[]){0x1F, 0x73},                                                                   2,  0  },
    {0xB6, (uint8_t[]){0x6B, 0x6B},                                                                   2,  0  },
    {0xD4, (uint8_t[]){0x02},                                                                         1,  0  },
    {0xBD, (uint8_t[]){0x01},                                                                         1,  0  },
    {0xB1, (uint8_t[]){0x00},                                                                         1,  0  },
    {0xBD, (uint8_t[]){0x00},                                                                         1,  0  },
    {0xBF, (uint8_t[]){0x40, 0x81, 0x50, 0x00, 0x1A, 0xFC, 0x01},                                     7,  0  },
    {0x3A, (uint8_t[]){0x50},                                                                         1,  0  },
    {0x11, (uint8_t[]){0x00},                                                                         1,  200},
    {0xB2, (uint8_t[]){0x00, 0x80, 0x64, 0x0C, 0x06, 0x2F, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x18},       12, 0  },
    {0x29, (uint8_t[]){0x00},                                                                         1,  80 }, // display on
};

// The SDIO pinout of the onboard ESP32-C6 co-processor follows Espressif's
// reference wiring for a P4+C6 pair and works on a rev v1.3 board (scan and
// connect both OK). Rather than require a rebuild+reflash if a revision needs
// something else, every pin can still be overridden at runtime from NVS (see
// the "sdio" serial console command in serial_console.cpp), falling back to
// the build-time SDIO2_* macros when no override is stored.
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
#define TOUCH_ADDR                                                                                           \
    0x5D // GT911 default I2C address (0x14 is the other legal
         // one -- SensorLib's probe tries both, and R108 that
         // wires GPIO2 to the touch INT is optional/not fitted
         // by default, so the address can't be forced here)

static bool touchReady = false;

// GT911 RST is a plain GPIO here, but it is driven through reset_cb (leaving
// pin_rst at -1) on purpose: hal_touch_init() then hands SensorLib -1 as its
// own reset pin, so hal_touch_read()'s touch.reset() after every reported
// point becomes a no-op. With a real pin_rst the base TouchDrvInterface::
// reset() pulses RST after each read, re-initialising the controller while
// the finger is still down -- the chip re-latches that finger as a brand-new
// touch and every on-screen keyboard key types twice.
static void touchResetCb(bool level) {
    launcherGpioOutput(TOUCH_RST_PIN);
    launcherGpioWrite(TOUCH_RST_PIN, level ? HIGH : LOW);
}

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_rst = -1; // RST goes through reset_cb instead, see above
    cfg.pin_irq = TOUCH_INT_PIN;
    cfg.reset_cb = touchResetCb;
    cfg.gt911_int_sync = true;
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

void _setup_gpio() {
    // Board I2C bus (GPIO7/GPIO8): GT911 touch here, the ES8311/ES7210
    // audio codec hangs off it too (unused by this port).
    Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);

    // Backlight boost converter enable (AP3032 EN through 0R R32), active
    // HIGH. Must be on before TFT_BL's PWM does anything.
    pinMode(TFT_BL_EN, OUTPUT);
    digitalWrite(TFT_BL_EN, HIGH);

    // Aim displayConfig at Waveshare's own HX8394 sequence. Runs before
    // tft->begin() -- the window where the struct is still writable and
    // nothing has been built from it yet.
    TFT_SET_DSI_INIT(waveshareHx8394InitOperations);
}

void _post_setup_gpio() {
    // Brightness control must be initialized after tft in this case @Pirata
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);

    touchReady = hal_touch_init(touchCfg(), TOUCH_ADDR);
    if (!touchReady) {
        launcherConsolePrintln("Touch IC not Started");
        log_i("Touch IC not Started");
    } else launcherConsolePrintln("Touch IC Started");

    // ESP32-P4 has no native radio; WiFi/BT come from the onboard ESP32-C6 over
    // SDIO. These pins work on a P4 rev v1.3 board (scan and connect both OK);
    // a different revision can be retried through an NVS override (see the
    // "sdio" serial command) without rebuilding.
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

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

void InputHandler(void) {
    if (!touchReady) return;
    // Apply a touch only on its press edge. The input task clears touchPoint
    // every pass and the menus/keyboard act on each applied press without
    // waiting for a release, so re-applying a held finger at the old 250 ms
    // cadence alone typed the next key while the finger was still down from
    // the previous one. A finger held past 500 ms repeats at 250 ms, same as
    // seeedstudio-wio-tracker-l2 (LongPress keeps re-applying every pass so
    // launcherSelectHeld() still sees the held state).
    static bool touchWasDown = false;
    static uint32_t touchDownAt = 0;
    static uint32_t touchAppliedAt = 0;
    LTouchPoint t;
    const bool touchDown = hal_touch_read(touchCfg(), t);
    const bool touchEdge = touchDown && !touchWasDown;
    touchWasDown = touchDown;
    if (!touchDown) return;
    const uint32_t now = launcherMillis();
    if (touchEdge) touchDownAt = now;
    const bool repeat = now - touchDownAt > 500 && now - touchAppliedAt > 250;
    if (touchEdge || repeat || LongPress) {
        touchAppliedAt = now;
        if (!hal_touch_apply(t)) return;
    }
}
