// Board-local GSL3670 capacitive touch driver for the Seeed reTerminal D1001.
//
// Ported (I2C protocol + init/read sequence) from Seeed's own ESP-IDF BSP
// (components/esp_lcd_touch_gsl3670/esp_lcd_touch_gsl3670.c), adapted from
// esp_lcd_panel_io_tx_param/rx_param to this project's Wire-based I2C access
// (the same pattern boards/elecrow-esp32p4-7in uses for its GT911 touch on
// the same P4 chip). Deliberately kept board-local per the porting brief --
// this project's touch HAL (src/hal/inputs/touch.cpp / TOUCH_CTRL_*) has no
// GSL3670 case and none is being added there.
//
// The firmware/config blobs the chip needs uploaded at init
// (gsl3670_fw_data.h) are vendored verbatim (opaque binary data) from the
// same BSP, Apache-2.0/MIT licensed like the rest of that BSP's own
// component code. gsl_point_id.cpp/.h is Silead's point-tracking algorithm,
// copied close to verbatim from the same BSP but carrying its own original
// GPL-2.0(-or-later) header -- see the top of that file.
#pragma once
#include <stdint.h>

struct Gsl3670Point {
    uint16_t x;
    uint16_t y;
};

// sda/scl: I2C bus pins (Wire1.begin() is called here). reset_set: callback
// that drives the touch chip's reset line -- on this board that is PCA9535
// expander bit 12 (BSP_LCD_TOUCH_RST), NOT a raw GPIO (see interface.cpp's
// d1001TouchResetSet() and connections.md for why: GPIO12 on the bare P4 is
// BSP_WIFI_BOOT, the ESP32-C6's boot-strap pin -- driving it as if it were
// touch reset toggled the co-processor's boot strap instead of resetting
// anything on the touch chip, which is why the chip always ACKed on I2C but
// its internal-MCU firmware upload never actually completed). Returns true
// once the chip answers with the expected RAM firmware signature.
bool gsl3670_init(int sda, int scl, void (*reset_set)(bool level));

// Polls the controller and fills points[] (up to max_points, native panel
// coordinates already mirrored per the BSP's mirror_x=1/mirror_y=1/swap_xy=0
// orientation). Returns the number of points filled (0 when nothing is
// touched or the read failed).
uint8_t gsl3670_read(Gsl3670Point points[], uint8_t max_points);
