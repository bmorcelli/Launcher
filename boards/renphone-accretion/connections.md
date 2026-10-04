# Accretion Phone — DIY ESP32-S3 phone

Home-made phone: ESP32-S3 module with 16MB flash and 8MB OPI PSRAM (N16R8),
2.8" ILI9341 240x320 SPI display, XPT2046 resistive touch, SD card on SDIO.
Display and touch use the same pinout as the generic
`esp32s3-ili9341-xpt2046` env; this env adds the SD card, battery sense and
the keep-off pins below.

## Display — ILI9341, 240x320, SPI (SPI2_HOST)

| Signal | GPIO |
| ------ | ---- |
| SCLK   | 12   |
| MOSI   | 11   |
| MISO   | 13   |
| DC     | 2    |
| CS     | 10   |
| RST    | 14   |
| BL     | 21 (PWM) |

Not IPS, no inversion, RGB order default (`TFT_IPS=0`).

## Touch — XPT2046, bit-banged on its own pins

| Signal | GPIO |
| ------ | ---- |
| CLK    | 6    |
| MOSI   | 5    |
| MISO   | 4    |
| CS     | 9    |
| IRQ    | not wired (-1), the driver polls |

These are **not** on the display SPI bus (`xpt_shared_spi = false` in
`hal_touch_init`). Touch orientation follows the generic 2.8" module table
(`CYD-2432S028`); if the corners come out swapped or mirrored on real
hardware, tune `CYD28_TouchR_ROT` (bit0 = swap X/Y, bit1 = invert X,
bit2 = invert Y) in `platformio.ini`, then run Settings > Calibrate Touch.

## SD card — native SDIO, 1-bit mode (not SPI)

| Signal | GPIO |
| ------ | ---- |
| CLK    | 39   |
| CMD    | 38   |
| D0     | 40   |

D1–D3 are not wired. `sd_functions.cpp` mounts it in 1-bit mode when
`SD_MMC_4BIT` is not set.

## Battery

- Single-cell Li-ion, 10k + 10k voltage divider (read value x2) on
  **GPIO 8** (`ANALOG_BAT_PIN`, ADC1_CH7 — safe to read with WiFi on).
- No PMIC or fuel gauge, the percentage is estimated from voltage by the
  shared `getBattery()`.
- GPIO 4 must **not** be used for the divider: it is the touch MISO line.

## Device specific initialization

- Vibration motor on GPIO 18 (`VIB_PIN`), driven LOW in `_setup_gpio()` so it
  stays off while the pin would otherwise float.
- WS2812 LED on GPIO 48 (`RGB_LED_PIN`), turned off in `_setup_gpio()` with the
  core's `neopixelWrite()`.
- TFT CS (10) and touch CS (9) are set HIGH before anything else starts.
- The launcher does not use audio, IMU or mic; the pins are recorded below
  so the next person does not reuse them.

## Pins used by the phone firmware, unused by the Launcher

| Function              | GPIO                          |
| --------------------- | ----------------------------- |
| I2S amp (MAX98357A)   | BCLK 42, LRC 41, DOUT 17      |
| I2S mic (INMP441)     | SCK 47, WS 46, SD 45          |
| MPU6050 (I2C 0x68)    | SDA 15, SCL 7                 |
| USB female (native)   | D- 19, D+ 20                  |

## Notes

- The module uses octal PSRAM, so GPIO 35–37 are reserved by the module and
  must stay unused (none of the pins above are in that range).
- Power off is a plain deep sleep; wake it with the EN/reset button.
