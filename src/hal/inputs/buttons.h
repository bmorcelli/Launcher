#ifndef LAUNCHER_HAL_INPUTS_BUTTONS_H
#define LAUNCHER_HAL_INPUTS_BUTTONS_H

#include "../device.h"

// GPIO button HAL (HAS_1_BUTTON..HAS_6_BUTTONS) -- raw pin polling for
// 1/3/5/6 buttons, ESP-IDF `button` component (interrupt-driven) for 2.
// Boards that read buttons through another abstraction (M5Unified, AXP192)
// are out of scope here -- see src/hal/README.md.
//
// cfg.btnN meaning depends on which hal_buttons_poll_* is used for the
// board's button count -- see each function below.

// Configures every pin used by `count` (1, 3, 5 or 6) as input, with or
// without internal pull-up per cfg.pullup.
void hal_buttons_init(const DeviceButtons &cfg, uint8_t count);

// 1 button (cfg.btn1 = Sel/Next/Prev/Esc, all on the same pin): short click
// -> Next, double click -> Prev, hold 550ms -> Sel, hold 1200ms -> Esc.
// Also draws the same hold-progress border on `tft` the original boards did.
void hal_buttons_poll_1(const DeviceButtons &cfg);

// 2 buttons (cfg.btn1, cfg.btn2), via the ESP-IDF `button` component --
// selected by build_flags macro BUTTONS_IDF_COMPONENT (analogous to
// PMIC_BQ25896/GAUGE_BQ27220; needs
// lib_deps = https://github.com/bmorcelli/ESP32_Button in the board's
// platformio.ini). Btn1: short click -> Next, hold long_press_ms -> Sel.
// Btn2: short click -> Prev, hold long_press_ms -> Esc. Call
// hal_buttons_init_2() once from _setup_gpio(), then hal_buttons_poll_2()
// from InputHandler() every cycle (it drains the callback-set flags on its
// own ~200ms debounce, same as the other hal_buttons_poll_* functions).
void hal_buttons_init_2(const DeviceButtons &cfg, uint16_t long_press_ms = 600);
void hal_buttons_poll_2();

// 3 buttons (cfg.btn1 = Prev, cfg.btn2 = Next, cfg.btn3 = Sel):
// Prev+Next held together -> Esc (instead of Prev/Next).
void hal_buttons_poll_3(const DeviceButtons &cfg);

// 5 buttons (cfg.btn1 = Prev, cfg.btn2 = Next, cfg.btn3 = Up, cfg.btn4 =
// Down, cfg.btn5 = Sel): Prev+Next held together -> Esc.
void hal_buttons_poll_5(const DeviceButtons &cfg);

// 6 buttons: same mapping as hal_buttons_poll_5 plus a dedicated Esc button
// (cfg.btn6). Pass esc_on_combo_too = true for boards where Prev+Next should
// *also* raise Esc (e.g. xueersi-xiaomiao); reaper only uses the dedicated
// button.
void hal_buttons_poll_6(const DeviceButtons &cfg, bool esc_on_combo_too = false);

#endif
