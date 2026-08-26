#include "hal/bright/bright.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

#ifndef TFT_BL
#define TFT_BL GPIO_BCKL
#endif

#if defined(TOUCH_CTRL_GT911) || defined(TOUCH_CTRL_CST8XX)
#include "hal/device.h"
#include "hal/inputs/touch.h"
// Per-rotation swap/mirror table, solved algebraically (not guessed) from
// composing the old driver-specific pre-transform (TouchLib's
// TOUCH_INVERTED for GT911, CYD28_TouchscreenC::convertRawXY for CST820)
// with InputHandler()'s old shared per-rotation remap block -- both are
// gone now, hal_touch_read() reproduces the exact same screen mapping in
// one step, the same way every other already-migrated GT911/CST8xx board
// does. See docs/etapa_7.md for the derivation.
static DeviceTouch touchCfg() {
    DeviceTouch cfg;
#if defined(TOUCH_CTRL_GT911)
    cfg.pin_sda = GT911_I2C_CONFIG_SDA_IO_NUM;
    cfg.pin_scl = GT911_I2C_CONFIG_SCL_IO_NUM;
    cfg.pin_rst = GT911_TOUCH_CONFIG_RST_GPIO_NUM;
    cfg.pin_irq = GT911_TOUCH_CONFIG_INT_GPIO_NUM;
#else
    cfg.pin_sda = CST816S_I2C_CONFIG_SDA_IO_NUM;
    cfg.pin_scl = CST816S_I2C_CONFIG_SCL_IO_NUM;
    cfg.pin_rst = CST816S_TOUCH_CONFIG_RST_GPIO_NUM;
    cfg.pin_irq = CST816S_TOUCH_CONFIG_INT_GPIO_NUM;
#endif
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
#include "CYD28_TouchscreenR.h" // also defines CYD28_TouchR_MOSI, used below to detect a shared SPI bus
#ifndef CYD28_DISPLAY_HOR_RES_MAX
#define CYD28_DISPLAY_HOR_RES_MAX 320
#endif

#ifndef CYD28_DISPLAY_VER_RES_MAX
#define CYD28_DISPLAY_VER_RES_MAX 240
#endif
#if defined(TOUCH_CTRL_XPT2046)
#include "hal/device.h"
#include "hal/inputs/touch.h"
static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    // rotation:        0      1      2      3
    bool swapXY[4] = {true, false, true, false};
    bool mirrorX[4] = {true, false, false, true};
    bool mirrorY[4] = {false, false, true, true};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}
#else
CYD28_TouchR touch(CYD28_DISPLAY_HOR_RES_MAX, CYD28_DISPLAY_VER_RES_MAX);
#endif
#endif

void _setup_gpio() {
#if defined(TOUCH_AXS15231B_I2C)
    Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
#endif
#if !defined(TOUCH_CTRL_CST8XX) && defined(CYD)
    // Was gated on !HAS_CAPACITIVE_TOUCH before that macro was replaced by
    // TOUCH_CTRL_CST8XX -- same two envs excluded (CYD-2432S022C/
    // CYD-2432W328C), where this pin doubles as the touch I2C SDA / TFT
    // data line and must not be forced to a plain output here.
    launcherGpioOutput(33); // CS Pin
#elif defined(CYDS3)
    launcherGpioOutput(38); // CS Pin
#endif
}

void _post_setup_gpio() {
    // Brightness control must be initialized after tft in this case @Pirata
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);

#if defined(TOUCH_CTRL_XPT2046)
    bool touchOk = hal_touch_init(touchCfg(), 0x5D, TFT_MOSI == CYD28_TouchR_MOSI);
#elif defined(TOUCH_CTRL_GT911)
    bool touchOk = hal_touch_init(touchCfg());
#elif defined(TOUCH_CTRL_CST8XX)
    bool touchOk = hal_touch_init(touchCfg(), 0x15 /* CST816_SLAVE_ADDRESS -- also CST820's address */);
#else
    bool touchOk = touch.begin(
#ifdef CYD28_TouchR_MOSI
#if TFT_MOSI == CYD28_TouchR_MOSI
        &SPI
#endif
#endif
    );
#endif
    if (!touchOk) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());
}

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

void InputHandler(void) {
    static long d_tmp = launcherMillis();
#if defined(TOUCH_CTRL_XPT2046) || defined(TOUCH_CTRL_GT911) || defined(TOUCH_CTRL_CST8XX)
    if (launcherMillis() - d_tmp > 250 || LongPress) {
        LTouchPoint t;
        if (hal_touch_read(touchCfg(), t)) {
            d_tmp = launcherMillis();
            if (!hal_touch_apply(t)) return;
        }
    }
#else
    bool touched = touch.touched();                    // read every cycle to skip bad readings
    if (launcherMillis() - d_tmp > 250 || LongPress) { // I know R3CK.. I Should NOT nest if statements..
        // but it is needed to not keep SPI bus used without need, it save resources
        LTouchPoint t;
        if (touched) {
            auto t = touch.getPointScaled();
            launcherConsolePrintf("\nBEF: Touch Pressed on x=%d, y=%d, rot: %d", t.x, t.y, rotation);
            d_tmp = launcherMillis();

            if (rotation == 3) {
                t.y = (tftHeight + (_fm * LH + 4)) - t.y;
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
                t.y = (tftHeight + (_fm * LH + 4)) - tmp;
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
