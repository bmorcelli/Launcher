Seeed Studio reTerminal E1001 -- ESP32-S3 (8 MB octal PSRAM, 32 MB flash),
800x480 e-paper, no touch, 3 physical nav buttons, microSD.

## e-paper display (SPI, shared with the microSD card)

Confirmed against Seeed's own GxEPD2 example:
https://github.com/Seeed-Projects/Seeed_GxEPD2/blob/1100ea37c16b910fd79152f4250c13d802b9c20b/examples/GxEPD2_reTerminal_E1001/GxEPD2_reTerminal_E1001.ino

| Signal | GPIO |
| ---    | :---: |
| SCLK   | 7    |
| MOSI   | 9    |
| CS     | 10   |
| DC     | 11   |
| RST    | 12   |
| BUSY   | 13   |

No MISO on the panel side -- it's write-only. Panel is the UC8179-family
800x480 glass, driven here with GxEPD2's `GxEPD2_750_GDEY075T7` class (same
command family as the reference example's driver).

## microSD (shares SCLK/MOSI with the e-paper, own CS/MISO/power enable)

Supplied by the person porting this board, not yet bench-confirmed:

| Signal    | GPIO |
| ---       | :---: |
| Power EN  | 16   |
| SCK       | 7    |
| MOSI      | 9    |
| MISO      | 8    |
| CS        | 14   |

Power-enable polarity is assumed active-HIGH (interface.cpp drives it HIGH
to power the card) -- confirm on hardware and fix if it turns out inverted.

Because SCLK/MOSI are shared, `_setup_gpio()` parks both CS pins HIGH before
calling `SPI.begin()` itself rather than letting GxEPD2 bring the bus up
(`GXEPD2_BEGIN_SPI` is not set).

## Buttons

Raw GPIO, active LOW with internal pull-up (`HAS_3_BUTTON`,
`hal_buttons_poll_3` -- Next+Prev held together also raises Esc):

| Button | GPIO |
| ---    | :---: |
| Sel    | 3    |
| Next   | 4    |
| Prev   | 5    |

## Battery (voltage divider, no fuel gauge chip)

Confirmed against Seeed's own example:
https://github.com/Seeed-Projects/OSHW-reTerminal-Series-E-D/blob/main/examples/base/Battery_Monitor/Battery_Monitor.ino

| Signal      | GPIO |
| ---         | :---: |
| Divider EN  | 21   |
| ADC input   | 1    |

GPIO21 must be driven HIGH for the board to run off battery at all (not just
to gate the ADC read) -- `_setup_gpio()` sets it once and leaves it HIGH for
the whole session. Battery percent comes from the shared weak `getBattery()`
in `src/mykeyboard.cpp` via `ANALOG_BAT_PIN=1`; its default 2x divider
multiplier already matches this board's divider, so no
`ANALOG_BAT_MULTIPLIER` override is needed.

## Not yet wired up / unknown

- No backlight/frontlight -- it's e-paper, `_setBrightness()` is a no-op.
- Default `ROTATION=0` (native landscape) is unconfirmed on hardware --
  Seeed's example never rotates the panel and the buttons sit on the top
  edge of the enclosure, but if the boot screen comes up sideways, adjust
  in `platformio.ini`.
