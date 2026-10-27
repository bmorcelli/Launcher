# Waveshare ESP32-C5-Touch-LCD-3.5

ESP32-C5 + ST7796 320x480 + FT6336 capacitive touch. Hardware twin of the
Pancake C5; the one structural difference is that the **panel reset and
backlight are not GPIOs** — a CH32V006 I/O expander at `0x24` on the shared I2C
bus owns panel power, the LCD reset, the FT6336 reset and the backlight PWM.

Pin map and bring-up order are from the hardware-verified
[ESP32-C5-BSP](https://github.com/slickrick773/ESP32-C5-BSP)
(`src/board/ws_c5_35/`).

## Buses

| Bus | Pins | Notes |
|---|---|---|
| Shared I2C | SDA = 27, SCL = 26 @ 100 kHz | expander (0x24), FT6336 (0x38), AXP2101 (0x34) — all share it |
| Display/SD SPI | MOSI = 7, MISO = 2, SCLK = 6 | ST7796 and the microSD share one SPI bus |

## Display — ST7796 320x480

| Signal | GPIO |
|---|---|
| MOSI | 7 |
| MISO | 2 |
| SCLK | 6 |
| CS | 8 |
| DC | 5 |
| RST | EXIO1 on the 0x24 expander (`TFT_RST=-1`) |
| Backlight | expander PWM register (`TFT_BL=-1`) |

Colour inversion is ON (`TFT_INVERSION_ON=1`) — Waveshare's init sends `0x21`.

## Touch — FT6336 capacitive

| Signal | Value |
|---|---|
| I2C address | 0x38 (HAL `TOUCH_CTRL_FT6X36`) |
| SDA / SCL | 27 / 26 (shared bus) |
| RST | EXIO0 on the 0x24 expander |
| INT | GPIO3 |

## microSD

CS = 9, on the display SPI bus. `_setup_gpio()` raises both the SD CS and the
LCD CS high before bring-up so the panel does not garble MISO on the shared bus
(the BSP's SD-before-display trap).

## Expander — CH32V006 @ 0x24

| Register | Addr |
|---|---|
| Mode / direction | 0x02 |
| Output latch | 0x03 |
| Backlight PWM | 0x05 |

EXIO0 → FT6336 reset, EXIO1 → ST7796 reset, EXIO5 → panel power / backlight
rail. `expanderInit()` powers the rail with both resets released, then pulses
each reset (active-low) with the rail up, and runs once (pulsing EXIO1 again
after the ST7796 is initialised would blank it).

## Power — AXP2101 @ 0x34

Read over raw I2C in `interface.cpp` (percentage reg `0xA4`, charge status reg
`0x01`) to avoid pulling XPowersLib onto the C5. The BSP's XPowersLib path
(`ws_c5_35_battery.*`) is the reference if finer readings are wanted.

## Not wired here

No GPS. The board carries an LC76G over I2C (BSP `ws_c5_35_gps.*`), but Launcher
is an app loader, not a wardriver, so it is left out to keep the port clean.

## Still to validate on hardware

- `TFT_INVERSION_ON` / `TFT_IPS` vs. actual panel colours.
- Touch rotation (`SwapXY`/`MirrorX`/`MirrorY`) vs. the installed orientation.
- AXP2101 register values vs. the BSP XPowersLib readings.
