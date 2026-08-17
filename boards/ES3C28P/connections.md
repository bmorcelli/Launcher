# ES3C28P — 2.8" ESP32-S3 IPS LCD Display Module

Module: ESP32-S3R8 (16MB flash, 8MB OPI PSRAM)
Vendor page: <https://www.lcdwiki.com/2.8inch_ESP32-S3_Display>

## Display — ILI9341V, 240x320 IPS, SPI

| Signal | GPIO |
| ------ | ---- |
| CS     | 10   |
| MOSI   | 11   |
| SCLK   | 12   |
| MISO   | 13   |
| DC     | 46   |
| RST    | tied to the module reset (-1) |
| BL     | 45 (PWM) |

The panel is IPS and needs inversion on, which `TFT_IPS=1` gives on the
Arduino_GFX backend.

## Touch — FT6336G, I2C0 @ 0x38

| Signal | GPIO |
| ------ | ---- |
| SDA    | 16   |
| SCL    | 15   |
| RST    | 18   |
| INT    | 17   |

The bus is shared with the ES8311 audio codec (0x18).

## SD card — native 4-bit SDIO (not SPI)

| Signal | GPIO |
| ------ | ---- |
| CLK    | 38   |
| CMD    | 40   |
| D0     | 39   |
| D1     | 41   |
| D2     | 48   |
| D3     | 47   |

## Audio — ES8311 codec

| Signal    | GPIO |
| --------- | ---- |
| Audio_EN  | 1 (active LOW, held off at boot) |
| I2S MCLK  | 4    |
| I2S BCLK  | 5    |
| I2S DOUT  | 6    |
| I2S LRCK  | 7    |
| I2S DIN   | 8    |

The launcher does not use audio; the pins are recorded here for future work.

## Misc

| Signal      | GPIO |
| ----------- | ---- |
| BOOT button | 0 (active LOW, used as Esc) |
| WS2812 LED  | 42   |
| Battery ADC | 9 (voltage divider — read value x2) |
| UART0 TX/RX | 43 / 44 |
| USB D-/D+   | 19 / 20 |
