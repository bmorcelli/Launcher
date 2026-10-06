# Elecrow ThinkNode M9

MeshCore/Meshtastic communication terminal with a full keyboard.
ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM), 2.4" 320x240
ST7789 LCD, 5x10 key keypad plus shortcut keys driven by a coprocessor,
LR1110 LoRa, AG3352/ATGM336H GNSS, PCF8563 RTC, QMC6309 magnetometer,
QMI8658B IMU, 2300 mAh battery with an LGS4056 charger.

Pins were taken from the Meshtastic variant
(`variants/esp32s3/ELECROW-ThinkNode-M9`) and the
[Elecrow wiki](https://www.elecrow.com/wiki/ThinkNode_M9_MeshCore_Communication_Terminal_with_Full_Keyboard.html).

## Modules sharing SPI bus (SPI3)
| Device        | SCK | MISO | MOSI | CS | DC | RST | BL |
| ---           | :-: | :-:  | :-:  | :-:| :-:| :-: | :-:|
| ST7789 LCD    | 40  | 38   | 47   | 16 | 15 | 14  | 17 |
| microSD       | 40  | 38   | 47   | 48 | -  | -   | -  |
| LR1110 LoRa   | 40  | 38   | 47   | 39 | -  | 45  | -  |

All three CS lines are driven HIGH in `_setup_gpio()` before the bus comes
up. The Launcher does not use the LoRa radio; its CS only needs to stay
deselected. LR1110 BUSY is GPIO41 and IRQ (DIO1) GPIO42.

## I2C bus 0 (SDA 20 / SCL 21) — keypad coprocessor only
`0x6C` on board revision v1.0, `0x6D` on v1.1. This bus is a private link
between the two MCUs (`ESP32-2_SDA` / `ESP32-2_SCL` in the schematic);
nothing else hangs off it.

## I2C bus 1 (SDA 7 / SCL 6)
PCF8563 RTC (`0x51`), QMC6309 magnetometer, QMI8658B IMU. All unused by the
Launcher, so the bus is never started.

## Keypad (coprocessor)
**There is not a single navigation button wired to the ESP32-S3.** Every
key — the 5x10 matrix *and* the dedicated keys (back, Home, Time, FM,
Preset, Mute) — is wired to a second MCU (an ESP32-S2 per the Elecrow wiki,
an STC8H per the Meshtastic port; the schematic nets are the coprocessor's
own GPIOs 33/34/38/39/18/10, which have nothing to do with the main
controller's pins of the same number). The only key-related line that
reaches the ESP32-S3 is `ESP32_WAKEUP` / KB_INT on GPIO12.

That rules out `HAS_5_BUTTON`/`HAS_6_BUTTON` on this board: those layouts
poll raw host GPIOs, and there are none to poll. Navigation has to come out
of the key codes read over I2C.

The host never sees the key matrix; it reads one key code from register
`0x01`. Two properties of that register decide how it has to be driven, and
both were confirmed on hardware:

- **The register latches.** The pending key stays there until it is read, so
  a key pressed now is still returned by a read minutes later.
- **KB_INT (GPIO12) is not a per-key interrupt.** Its schematic net is
  `ESP32_WAKEUP`: the coprocessor raises it to wake a sleeping host, and
  only for some keys (Select/Enter does, the arrows do not). Gating the read
  on that line loses almost every press — the symptom is that an arrow press
  does nothing until Enter is pressed, which then reports the *earlier*
  arrow. So `InputHandler()` **polls** the register every cycle. The
  reference firmware does the same (polls every 300 ms and only uses the
  line to cut that wait short).

The register reads `0x88` ("invalid key") when drained. The register
address must also be sent as a complete transaction (STOP) before the read —
a repeated start is not answered.

| Register | Meaning |
| ---      | --- |
| `0x01`   | pending key code, latched until read |
| `0x03`   | long-press window, 2 bytes big-endian ms (set to 700) |
| `0x05`   | reads `0xFF` — *not* the key register, despite the reference source naming it `MATRIX_KEY` |
| `0x06`   | write `0x01` to put the coprocessor to sleep |

Key codes are printable ASCII for the alphanumeric keys plus the following.
Every code in this table was confirmed on hardware:

| Code | Key | Launcher action |
| :-:  | --- | --- |
| `0xB4` / `0xB5` / `0xB6` / `0xB7` | Left / Up / Down / Right | Prev / Up / Down / Next |
| `0x0D` | Enter | Sel |
| `0x08` / `0x89` | Del / Del long press | Esc + backspace |
| `0x86` | FM long press | Esc |
| `0x87` | Preset | Sel long press (folder/file options: delete, rename, ...) |
| `0x81`-`0x85` | Mute, Home, Time, GPS, FM | unmapped |

The coprocessor never reports a key as *held*, and Enter has no long-press
code, so the SD browser's "hold Sel" check can't see Enter held. **Preset**
stands in for it (same as Meshtastic's M9 port, where Preset is
`SELECT_LONG`): it selects, then keeps Sel reported as held while a
long-press check runs.
| `0x88` | no/invalid key | ignored (the drained value) |

> The reference driver also reads the battery millivolts from `0x01`..`0x04`
> little-endian, which contradicts its own use of `0x01` as the key register.
> `0x01` is confirmed to be the key register, so the Launcher reads the
> battery from the ADC instead (see below).

**KB_LED (GPIO46)** is the keypad backlight: a plain active-HIGH GPIO, not
an I2C command and not PWM. It follows the screen backlight on/off state in
`_setBrightness()`.

Because every key is behind the coprocessor, KB_INT is the only GPIO that
can gate the boot — `LAUNCHER_GPIO_KEYS` exposes it as "Any Key", and the
GPIO level for it must be set to **1**. Since the line is only raised for
some keys, hold **Enter** at boot; that one is confirmed to raise it.

## Battery
GPIO13 through a 2x divider (`ANALOG_BAT_PIN=13`, the shared
`getBattery()` in `src/mykeyboard.cpp` already assumes 2x). GPIO13 is
**ADC2**_CH2, which the Wi-Fi radio also owns on the ESP32-S3: readings can
come back as 0 (reported as 1%) while Wi-Fi is up. It is the only battery
sense the board exposes. USB power presence is on GPIO1, active LOW
(unused).

## Display specifics
- Backlight (GPIO17) is **active LOW**: duty 0 is fully lit. Handled by
  `HalBrightCurve.invert` so the brightness curve keeps the shared feel.
- The panel needs inverted colours (`TFT_IPS=1`), no row/column offsets.
- `ROTATION=3` matches the reference firmware's landscape orientation
  (screen above the keyboard); it has not been confirmed on hardware yet —
  flip to 1 if the image comes up upside down.

## Device specific initialization
- **VEXT_EN (GPIO18) must be driven LOW first**: it is the peripheral rail
  that powers the display. Nothing works until it is on.
- GPS: `GPS_SLEEP_INT` (GPIO10) must rest HIGH, and `GPS_EN` (GPIO11) is
  left at the *disabled* level since the Launcher has no use for the GNSS.
  The polarity differs per revision — v1.0 enables LOW, v1.1 enables HIGH —
  so the keypad's I2C address is used to pick it.
- The USB-C port goes through a **CH340K**, not the ESP32-S3 native USB:
  `ARDUINO_USB_CDC_ON_BOOT=0` (console is UART0 on GPIO43/44) and
  `DISABLE_MASS_STORAGE=1` (USB MSC can never enumerate).
- `powerOff()` puts the coprocessor to sleep, drops VEXT, and deep-sleeps with
  ext1 wake on KB_INT going HIGH, so any key press powers the board back up.
- Buzzer on GPIO9, display tearing-effect line on GPIO19 — both unused.
