# Seeed SenseCAP Indicator

ESP32-S3 (8MB flash, octal PSRAM) + ST7701 480x480 RGB panel + FT6336-family
capacitive touch + RP2040 sensor coprocessor + SX1262 LoRa radio. Only the
display, touch and the single button are wired up here -- the RP2040
coprocessor (sensor readings) and the LoRa radio are outside Launcher's scope
and left untouched. **No SD card is wired to the ESP32-S3 on this board.**

Pin mapping below comes from Meshtastic's own variant for this board
(`variants/esp32s3/seeed-sensecap-indicator/variant.h`), cross-checked against
Seeed's own `SenseCAP_Indicator_ESP32` SDK
(`components/bsp/src/boards/sensecap_indicator_board.c` and
`lcd_panel_config.c`, and `components/i2c_devices/touch_panel/ft5x06.c`),
which was also used to verify the ST7701 init table byte-for-byte and the
touch controller's address.

## I2C bus (Wire)
- SDA: GPIO39
- SCL: GPIO40
- IO expander: XL9555/PCA9535-compatible (Seeed's own SDK calls it a
  TCA9535), address `0x20` (`lib/SensorLib`'s `IoExpanderXL9555`). Seeed's own
  board-detect code also falls back to probing `0x39` on some revisions --
  `_setup_gpio()` does the same. Its own INT line is on GPIO42, unused here
  (nothing on this board is read through the expander's interrupt).
- Touch controller (FT6336-family, "FT6336U" per Seeed's own driver):
  address `0x48`.

## IO expander pin map (see `interface.cpp`)
| Expander pin | Function          | Notes |
| ---          | ---               | ---   |
| 0             | LoRa CS           | Unused -- Launcher has no LoRa support |
| 1             | LoRa RESET        | Unused |
| 2             | LoRa BUSY         | Unused |
| 3             | LoRa DIO1         | Unused |
| 4             | LCD (ST7701) CS   | Held low permanently once at boot -- see below |
| 5             | LCD (ST7701) RESET| Held high, never pulsed -- matches Seeed's own driver, which never toggles it either |
| 6             | Touch INT         | Unused -- the FT6X36 HAL driver polls instead of using IRQ |
| 7             | Touch RESET       | Pulsed low/high once at boot |
| 8             | RP2040 sensor coprocessor power enable | Unused -- no sensor support here |

## Display: ST7701, 480x480
Driven two ways at once, both required:
- **Register init**, over a 3-wire SPI sideband (no DC pin): SCK=GPIO41,
  MOSI=GPIO48, MISO=GPIO47. This is `TFT_RGB_INIT_BUS` in `platformio.ini`.
- **Pixel data**, over the RGB parallel bus: DE=18, VSYNC=17, HSYNC=16,
  PCLK=21, R0-4={4,3,2,1,0}, G0-5={10,9,8,7,6,5}, B0-4={15,14,13,12,11}.
- Backlight: GPIO45, plain PWM (`hal_bright_*`).

CS and RESET for the SPI sideband are **not** native GPIOs -- both are wired
to the IO expander (pins 4 and 5 above). Arduino_GFX's `Arduino_SWSPI`/
`Arduino_RGB_Display` only ever get a CS/RST they can toggle directly, so
both are passed as "not defined" in `platformio.ini` and driven through the
expander instead, in `_setup_gpio()`. The same physical SCK/MOSI/MISO pins
also carry the LoRa radio (its own CS/RESET are expander pins 0/1), but since
Launcher never initializes the radio, the display's CS is asserted once and
left low for good rather than toggled per transfer.

Init table: `st7701_type1_init_operations` (one of the ready-made ST7701
tables Arduino_GFX ships in `display/Arduino_RGB_Display.h`). Confirmed
byte-for-byte against Seeed's own `lcd_panel_st7701s_init()` (the same
0xFF page selects, 0xC0/0xC1/0xC2/0xCD/0xB0/0xB1/0xE0-0xED register values),
except that Seeed's driver also sends `0x36 0x10` (MADCTL) right before the
final page-0 select, which the Arduino_GFX table omits -- worth trying if
colors or orientation look wrong once the panel is otherwise alive.

## Touch: FT6336-family (I2C)
- Address `0x48` (not the HAL's `0x5D` default -- passed explicitly to
  `hal_touch_init()`).
- RESET is on the IO expander (pin 7), pulsed once at boot; INT is also on
  the expander but unused, since `TOUCH_CTRL_FT6X36`'s driver polls rather
  than using an interrupt.

## Button
- Single button, GPIO38, active LOW with pull-up (`HAS_1_BUTTON`).

## Not wired up
- No SD card.
- No PMIC/fuel gauge -- battery percentage falls back to 0 (no gauge chip
  identified on this board).
- LoRa radio (SX1262) and the RP2040 sensor coprocessor (UART on GPIO19/20)
  are present on the hardware but outside Launcher's scope.
