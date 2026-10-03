#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>

// Seeed Studio XIAO 1.47" Touch Display, on a XIAO ESP32-S3 Plus.
// GPIOs below are the ESP32-S3 numbers; the wiki documents the board in XIAO
// D-numbers, and connections.md carries the full D-number <-> GPIO table.
#define TOUCH_SDA 5  // D4
#define TOUCH_SCL 6  // D5
#define TOUCH_INT 44 // D7
#define BTN_USR1 11  // D19
#define BTN_USR2 42  // D15

static bool touch_OK = false;

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_sda = TOUCH_SDA;
    cfg.pin_scl = TOUCH_SCL;
    // The AXS5106L's RST is the panel's RST line. By the time touch comes up
    // (_post_setup_gpio) the panel is already initialized, so pulsing it would
    // blank the screen -- leave it at -1 and rely on the panel's own reset.
    cfg.pin_rst = -1;
    cfg.pin_irq = TOUCH_INT;

    // The controller reports in the panel's native portrait range, with the
    // same column order the panel shows when MADCTL's MX bit is clear. These
    // undo each rotation's MADCTL back to that frame. The HAL indexes them by
    // `rotation & 3`, and this board's rotations are the MX-based family
    // (ROT_OFFSET=4), so the index is the rotation minus 4:
    //   index 0 -> rotation 4 (MX)       portrait  172x320
    //   index 1 -> rotation 5 (MX|MY|MV) landscape 320x172
    //   index 2 -> rotation 6 (MY)       portrait  172x320
    //   index 3 -> rotation 7 (MV)       landscape 320x172, the shipped one
    // Only 5 and 7 are reachable from the rotation menu: the panel is under
    // 200px, so gsetRotation() offers no portrait.
    cfg.MirrorX[0] = true;
    cfg.MirrorY[0] = false;
    cfg.SwapXY[0] = false;
    cfg.MirrorX[1] = true;
    cfg.MirrorY[1] = true;
    cfg.SwapXY[1] = true;
    cfg.MirrorX[2] = false;
    cfg.MirrorY[2] = true;
    cfg.SwapXY[2] = false;
    cfg.MirrorX[3] = false;
    cfg.MirrorY[3] = false;
    cfg.SwapXY[3] = true;
    return cfg;
}

/***************************************************************************************
** Function:    _setup_gpio()
** Location:    main.cpp
** Description: initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    // The microSD shares the panel's SPI bus: park its CS high before the
    // panel starts talking, so it can't answer the panel's traffic. GPIO43 is
    // also UART0 TX, which is why the console is on USB CDC on this board.
    launcherGpioOutput(SDCARD_CS);
    launcherGpioWrite(SDCARD_CS, HIGH);

    // Attach here, not in _post_setup_gpio(): main.cpp calls setBrightness()
    // right after tft->begin() and before _post_setup_gpio(), and leaving the
    // backlight at duty 0 until then hides the panel's init garbage.
    hal_bright_attach(TFT_BL);

    hal_buttons_init_2(DeviceButtons{BTN_USR1, BTN_USR2}, 600);
}

/***************************************************************************************
** Function:    _post_setup_gpio()
** Location:    main.cpp
** Description: second stage gpio setup, run after TFT and before SD card initialization
***************************************************************************************/
void _post_setup_gpio() {
    // After the panel: touch shares its RST line (see touchCfg()).
    touch_OK = hal_touch_init(touchCfg());
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    if (touch_OK) {
        static unsigned long tm = 0;
        LTouchPoint t;
        if (hal_touch_read(touchCfg(), t)) {
            if ((launcherMillis() - tm) > 200 || LongPress) {
                tm = launcherMillis();
                if (!hal_touch_apply(t)) return;
            }
        }
    }

    // USR1 short click -> Next, double click/hold -> Sel;
    // USR2 short click -> Prev, double click/hold -> Esc.
    hal_buttons_poll_2();
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    _setBrightness(0);
    // No PMIC on this board -- deep sleep, waking on USR1 (active low).
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(BTN_USR1), LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
