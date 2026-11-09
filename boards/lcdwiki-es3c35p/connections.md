# lcdwiki-es3c35p pinout

3.5" ESP32-S3 (16MB flash, 8MB PSRAM), ST77922 panel over QSPI with the
capacitive touch block built into the same controller IC. Sibling of
[ES3C28P](../ES3C28P/connections.md) (2.8", standard SPI).
Sold rebadged, e.g. "Hosyond ESP32-S3 Touchscreen Module 3.5"" (Amazon B0H28X8SQ4).

Sources: jlmeredith/ES3C35P `docs/HARDWARE.md`, `78/xiaozhi-esp32`'s
`boards/lcdwiki-es3c35p/`, and LCDwiki's published Arduino examples
(github.com/ydedox/st77922, Example_01 for the display, `esp_lcd_st77922.c` in
Example_08 for the touch register map).

## Display -- ST77922, QSPI, no DC pin

| Pin | GPIO | Notes |
| --- | ---- | ----- |
| CS   | 10 | |
| SCLK | 12 | |
| D0   | 11 | QSPI SIO0 / MOSI |
| D1   | 13 | QSPI SIO1 / MISO |
| D2   | 14 | QSPI SIO2 / WP |
| D3   |  9 | QSPI SIO3 / HD |
| RST  | -- | No reset pin; the display half needs none. |
| BL   | 41 | Backlight, active HIGH (LEDC PWM) |

All six QSPI pins are the ESP32-S3's FSPI IOMUX pins, which the driver requires
(`SPICOMMON_BUSFLAG_IOMUX_PINS`). If SPI bus init ever fails with "not using iomux
pins", check that `TFT_CS`/`TFT_SCLK` really are 10/12 at compile time.

**`TFT_DC=-1` and `TFT_IPS=0` are load-bearing.** `include/pre_compiler.h` has a
"CYD defaults" block that silently redefines `TFT_CS`/`TFT_SCLK` (to 15/14) when a
board defines none of `TFT_DC`, `TFT_IPS`, `TFT_COL_OFS*`, `TFT_MOSI`. A QSPI board
has no DC or MOSI pin, so it must opt out explicitly, as `platformio.ini` does.

Driven through `Arduino_ST77922_1L4` (`TFT_DATABUS_N=6` /
`TFT_DISPLAY_DRIVER_N=52`, `lib/DisplayDrivers/src/panels/Arduino_ST77922_1L4.h`),
a raw ESP-IDF `spi_master` driver modelled on the vendor's own Example_01 --
Arduino_GFX's `Arduino_ESP32QSPI` hardcodes every phase onto 4 lines, while this
panel wants register writes on 1 line (opcode 0x02, address `reg << 8`) and pixel
data on 4 (opcode 0x32, address 0x003C00). The init table is the vendor's active
`st77922_lcd_init[]`, unmodified.

Panel is native 320x480 **portrait**. `ROTATION=1` is the landscape mounting; the
panel's MADCTL does not remap addressing, so rotation is done in software by
`USE_CANVAS` (Arduino_Canvas) plus the driver's transform. Orientation and colour
order (RGB, no inversion) are confirmed correct on hardware at rotation 1.

## Touch -- built into the ST77922, I2C 0x55, NOT FT6x36-compatible

| Pin | GPIO | Notes |
| --- | ---- | ----- |
| SDA | 38 | shared with the ES8311 audio codec (addr 0x18) |
| SCL | 39 | |
| RST | 48 | idles **LOW**; the touch half does not answer until it is released (pulsed in `_setup_gpio()`). The display does not need it. |
| INT | 47 | active-low hint; the driver also polls at ~60 Hz |

`TOUCH_CTRL_ST77922` is a self-contained driver in `src/hal/inputs/touch.cpp`
(SensorLib has none). 16-bit big-endian register addresses, repeated-start reads:
`0x0010` info byte (bit 3 = report available), `0x0009` max touches, `0x0014`
report block of `maxTouches x 8` bytes (`[valid:1|rsvd:1|x_h:6] x_l y_h y_l area
intensity rsvd rsvd`). **The whole block must be read in one transfer** -- reading
only the first record leaves the controller returning the same stale point forever.
Reports raw panel-native portrait coordinates (x 0-319, y 0-479), so the host-side
`SwapXY`/`MirrorX`/`MirrorY` table in `interface.cpp` applies. Rotation 1 is verified
on hardware (swap + mirror Y); rows 0/2/3 were also verified by the user.

## SD card -- native 4-bit SDIO, dedicated bus

| Pin | GPIO |
| --- | ---- |
| CLK | 5 |
| CMD | 4 |
| D0  | 6 |
| D1  | 7 |
| D2  | 2 |
| D3  | 3 |

Not shared with anything else on this board. Verified working with a card inserted.
With no card the boot log reports `sdmmc_init_ocr ... 0x107` / "Failed to mount
SDCARD", which is expected.

## Other pins

| Function | GPIO | Notes |
| -------- | ---- | ----- |
| WS2812 status LED | 40 | Single LED, driven via `neopixelWrite()` |
| SC8002B audio amp shutdown | 1 | Active LOW; held HIGH (disabled) at boot |
| BOOT button | 0 | Only physical key; doubles as Esc |
| Battery sense | 8 | ADC1, 1:2 resistor divider (`ANALOG_BAT_PIN=8`, default `ANALOG_BAT_MULTIPLIER` 2.0). Found by an ADC scan of the unclaimed pins; verified against a multimeter (ADC 2.007 V x 2 = 4.01 V vs 3.99-4.00 V measured while charging). With USB attached the charger holds the rail near 4.1 V, so it reads high and Launcher shows the charging icon. |
| Free/unclaimed | 45, 46 | Per HARDWARE.md |

## Status

Verified on real hardware (COM6, 2026-11-09): display (orientation, colours,
backlight PWM), capacitive touch (menu navigation, on-screen keyboard / Wi-Fi
password entry), SD card, and all four rotations (display and touch, switched at
runtime from the Launcher Rotation menu), and battery sense (GPIO 8, checked
against a multimeter with a 3.7 V 850 mAh LiPo). Not yet verified: audio,
deep sleep / wake.
