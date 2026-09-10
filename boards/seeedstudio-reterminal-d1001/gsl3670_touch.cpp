// See gsl3670_touch.h. I2C register protocol, reset/init sequence and the
// touch-point readout ported from Seeed's ESP-IDF BSP
// (esp_lcd_touch_gsl3670.c), swapping esp_lcd_panel_io_tx_param/rx_param for
// plain Wire register reads/writes.
//
// Uses Wire1, not Wire: this touch controller lives on BSP_I2C_0 (GPIO37/38),
// a separate physical I2C peripheral from the PCA9535 IO expander's
// BSP_I2C_1 bus (GPIO20/21, plain Wire, see interface.cpp). Confirmed on real
// hardware that sharing one TwoWire object between both via repeated
// Wire.begin() calls breaks touch entirely -- same collision
// lilygo-t-display-p4/interface.cpp already documents and avoids the same
// way (see its "T-Display-P4 Keyboard add-on" comment block).
#include "gsl3670_touch.h"
#include "gsl3670_fw_data.h"
#include "gsl_point_id.h"
#include "idf/launcher_platform.h"
#include <Wire.h>

#define GSL3670_I2C_ADDR 0x40
#define GSL3670_REG_XY 0x80

static void (*s_reset_set)(bool level) = nullptr;
static uint8_t s_finger_num = 0;

// Panel size the BSP's touch config uses (esp32_p4_re_terminal_d1001.c
// bsp_touch_new(): x_max=800, y_max=1280, mirror_x=1, mirror_y=1, swap_xy=0).
static const uint16_t GSL3670_X_MAX = 800;
static const uint16_t GSL3670_Y_MAX = 1280;

static bool gsl3670_write(uint8_t reg, const uint8_t *data, uint8_t len) {
    Wire1.beginTransmission(GSL3670_I2C_ADDR);
    Wire1.write(reg);
    if (len) Wire1.write(data, len);
    return Wire1.endTransmission() == 0;
}

static bool gsl3670_write32(uint8_t reg, uint32_t val) {
    uint8_t buf[4] = {
        (uint8_t)(val & 0xff),
        (uint8_t)((val >> 8) & 0xff),
        (uint8_t)((val >> 16) & 0xff),
        (uint8_t)((val >> 24) & 0xff)
    };
    return gsl3670_write(reg, buf, 4);
}

static bool gsl3670_reg_read(uint8_t reg, uint8_t *data, uint8_t len) {
    Wire1.beginTransmission(GSL3670_I2C_ADDR);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom((int)GSL3670_I2C_ADDR, (int)len) != len) return false;
    for (uint8_t i = 0; i < len; i++) data[i] = Wire1.read();
    return true;
}

// Reset is driven through the PCA9535 IO expander (see gsl3670_touch.h) via
// the reset_set callback the board provides -- NOT a raw GPIO. Confirmed on
// real hardware that driving GPIO12 directly (this file's original
// approach) never actually reset the touch chip: GPIO12 on the bare P4 is
// BSP_WIFI_BOOT, unrelated silicon.
static void gsl3670_reset_pulse(uint32_t low_ms, uint32_t high_ms) {
    if (!s_reset_set) return;
    s_reset_set(false);
    launcherDelayMs(low_ms);
    s_reset_set(true);
    launcherDelayMs(high_ms);
}

static void gsl3670_clear_reg() {
    gsl3670_reset_pulse(20, 20);
    uint8_t v;
    v = 0x01;
    gsl3670_write(0x88, &v, 1);
    launcherDelayMs(5);
    v = 0x04;
    gsl3670_write(0xe4, &v, 1);
    launcherDelayMs(5);
    v = 0x00;
    gsl3670_write(0xe0, &v, 1);
    launcherDelayMs(20);
}

static void gsl3670_reset() {
    gsl3670_reset_pulse(20, 20);
    uint8_t v = 0x04;
    gsl3670_write(0xe4, &v, 1);
    launcherDelayMs(10);
    uint8_t zero[4] = {0, 0, 0, 0};
    gsl3670_write(0xbc, zero, 4);
    launcherDelayMs(10);
}

// Uploads the GSL3670's internal-MCU firmware image, gsl3670_fw[] (opaque
// vendor binary data, see gsl3670_fw_data.h) -- required every power-up,
// the chip has no on-board flash of its own.
static void gsl3670_load_fw() {
    const size_t n = sizeof(gsl3670_fw) / sizeof(gsl3670_fw[0]);
    for (size_t i = 0; i < n; i++) {
        uint8_t addr = gsl3670_fw[i].offset;
        if (addr == 0xf0) {
            uint8_t b = (uint8_t)(gsl3670_fw[i].val & 0xff);
            gsl3670_write(addr, &b, 1);
        } else {
            gsl3670_write32(addr, gsl3670_fw[i].val);
        }
    }
}

static void gsl3670_startup_chip() {
    uint8_t v = 0x00;
    gsl3670_write(0xe0, &v, 1);
    launcherDelayMs(10);
    gsl_DataInit(gsl3670_config_data);
}

static bool gsl3670_read_ram_fw_ok() {
    launcherDelayMs(30);
    uint8_t buf[4] = {0, 0, 0, 0};
    bool readOk = gsl3670_reg_read(0xb0, buf, 4);
    launcherConsolePrintf(
        "GSL3670: 0xb0 readback ok=%d bytes=%02X %02X %02X %02X\n", readOk, buf[0], buf[1], buf[2], buf[3]
    );
    if (!readOk) return false;
    return buf[0] == 0x5a && buf[1] == 0x5a && buf[2] == 0x5a && buf[3] == 0x5a;
}

// Bare I2C probe (no register access) -- tells apart "chip absent from the
// bus" (wrong pins/reset stuck/no power) from "chip present but the
// firmware handshake failed" (see gsl3670_read_ram_fw_ok() readback above).
static bool gsl3670_probe_ack() {
    Wire1.beginTransmission(GSL3670_I2C_ADDR);
    return Wire1.endTransmission() == 0;
}

bool gsl3670_init(int sda, int scl, void (*reset_set)(bool level)) {
    s_reset_set = reset_set;
    if (s_reset_set) s_reset_set(true); // deasserted (active LOW)

    Wire1.begin(sda, scl);
    launcherDelayMs(5);
    launcherConsolePrintf("GSL3670: I2C probe (before reset) ack=%d\n", gsl3670_probe_ack());

    gsl3670_clear_reg();
    gsl3670_reset();
    launcherConsolePrintf("GSL3670: I2C probe (after reset) ack=%d\n", gsl3670_probe_ack());
    gsl3670_load_fw();
    gsl3670_startup_chip();
    gsl3670_reset();
    gsl3670_startup_chip();

    return gsl3670_read_ram_fw_ok();
}

uint8_t gsl3670_read(Gsl3670Point points[], uint8_t max_points) {
    uint8_t touch_data[44];
    if (!gsl3670_reg_read(GSL3670_REG_XY, touch_data, sizeof(touch_data))) return 0;

    struct gsl_touch_info cinfo = {0};
    cinfo.finger_num = touch_data[0];
    if (cinfo.finger_num > 10) cinfo.finger_num = 10; // clamp -- 44-byte read only fits so many
    for (int j = 0; j < cinfo.finger_num; j++) {
        uint16_t x_hi = touch_data[(j + 1) * 4 + 3] & 0x0f;
        uint16_t x_lo = touch_data[(j + 1) * 4 + 2];
        cinfo.x[j] = (x_hi << 8) | x_lo;
        uint16_t y_hi = touch_data[(j + 1) * 4 + 1];
        uint16_t y_lo = touch_data[(j + 1) * 4 + 0];
        cinfo.y[j] = (y_hi << 8) | y_lo;
        cinfo.id[j] = (touch_data[(j + 1) * 4 + 3] & 0xf0) >> 4;
    }

    // Silead's "no-ID" point-tracking pass -- resolves raw sensor blobs into
    // stable per-finger coordinates/ids across frames (gsl_point_id.cpp).
    gsl_alg_id_main(&cinfo);
    uint32_t mask = gsl_mask_tiaoping();
    if (mask > 0 && mask < 0xffffffff) {
        uint8_t zero[4] = {0, 0, 0, 0};
        gsl3670_write(0xf0, zero, 4);
        gsl3670_write32(0x08, mask);
    }

    s_finger_num = (uint8_t)cinfo.finger_num;
    uint8_t count = s_finger_num < max_points ? s_finger_num : max_points;
    for (uint8_t i = 0; i < count; i++) {
        uint16_t rx = (uint16_t)cinfo.x[i];
        uint16_t ry = (uint16_t)cinfo.y[i];
        // BSP orientation: mirror_x=1, mirror_y=1, swap_xy=0.
        points[i].x = (rx < GSL3670_X_MAX) ? (GSL3670_X_MAX - 1 - rx) : 0;
        points[i].y = (ry < GSL3670_Y_MAX) ? (GSL3670_Y_MAX - 1 - ry) : 0;
    }
    return count;
}
