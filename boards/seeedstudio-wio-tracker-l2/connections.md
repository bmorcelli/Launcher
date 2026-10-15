Seeed Studio Wio Tracker L2 -- Wio-S3 module (ESP32-S3 + SX1262), 16MB flash,
8MB octal PSRAM, 3.2" 240x320 IPS touchscreen, microSD, 3000mAh Li-ion.

Pinout taken from Meshtastic's `variants/esp32s3/seeed_wio_tracker_L2`
(`variant.h`, `src/platform/extra_variants/seeed_wio_tracker_l2/variant.cpp`,
and the inline LGFX setup in `src/graphics/TFTDisplay.cpp`).
Wiki: https://wiki.seeedstudio.com/meshtastic_wio_tracker_l2_intro/

## Display -- NV3031B on QSPI (own SPI host, SPI3)
| Signal | GPIO |
| ---    | :---: |
| SCLK   | 42 |
| IO0    | 41 |
| IO1    | 40 |
| IO2    | 39 |
| IO3    | 38 |
| CS     | 46 |
| RST    | expander P06 |
| Power  | expander P05 (high = on) |

- Driver `TFT_DISPLAY_DRIVER_N=51` (`Arduino_NV3031B`, in DisplayDrivers'
  `src/panels/`), on `Arduino_ESP32QSPI`. SPI mode 3, 40MHz.
- Native 240x320 portrait; `ROTATION=1` (MADCTL MY|MV) is the landscape the
  case is built for. Needs INVON (`TFT_IPS=1`), RGB order.
- SLPOUT leaves the panel shifted by one column, so the driver's
  `displayOn()` does a full SWRESET + init instead.
- Expander P04 is named `LCD_CS` in Meshtastic but the panel CS is GPIO46;
  P04 is just parked high, same as Meshtastic does.

## Backlight -- LP5814 (I2C 0x2C)
4 LED channels driven together; PWM registers 0x18..0x1B, fed by
`hal_bright_curve()`. Initialized in `_setup_gpio()` because `setBrightness()`
runs before `_post_setup_gpio()`.

## I2C bus (SDA 47, SCL 48, 100kHz)
| Address | Device |
| ---     | --- |
| 0x21 | PCA9555/TCA9535 IO expander (XL9555 driver), INT on GPIO45 |
| 0x5D | GT911 touch |
| 0x2C | LP5814 backlight |
| 0x48 | ADS1115 ADC (battery on AIN0, 1:2 divider) |
| 0x22 | AW35615 USB-C CC controller (unused) |
| 0x18 | ES8311 audio codec (unused) |

## IO expander pins
| Pin | Function | Launcher sets |
| --- | --- | --- |
| P00 | WAKE button (active low) | input, read as Esc |
| P02 | SD detect | -- |
| P03 | Touch INT | low during touch reset (selects 0x5D), then input |
| P04 | "LCD_CS" | high |
| P05 | LCD power | high |
| P06 | LCD reset | pulsed |
| P07 | Grove power | -- |
| P08 | Touch reset | pulsed |
| P09 | GNSS reset | -- |
| P11 | USB OTG enable | low |
| P12 | Speaker amp enable | low |
| P13 | GNSS power | low |
| P14 | SD power | high |
| P15 | Battery ADC enable | high |

## microSD -- SDMMC 1-bit
CLK 2, CMD 3, D0 1; powered from expander P14.

## Inputs
- BOOT/User button, GPIO0 -- `HAS_1_BUTTON` (click Next, double click Prev,
  hold Sel, long hold Esc).
- WAKE button on expander P00 -- Esc.
- GT911 touch, polled (INT is on the expander). Digitizer is mounted 180
  degrees from the panel's native orientation; the Swap/Mirror tables in
  `interface.cpp` are derived from that and still need a 4-corner check on
  hardware.

## Unused here
SX1262 LoRa (SCK 4, MISO 5, MOSI 6, CS 21 held high, RST 7, DIO1 9, BUSY 8),
L76K GNSS (TX 17, RX 18), ES8311/ES7243E audio (I2S 10/11/12/15/16).

## Power off
Backlight, LCD, SD and battery ADC rails off, then deep sleep with wake on
GPIO0. There is no PMIC; the physical power switch cuts the battery.
