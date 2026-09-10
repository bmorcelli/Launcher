Seeed Studio reTerminal D1001 -- ESP32-P4 main SoC + ESP32-C6 co-processor
(SDIO, WiFi/BT), 800x1280 portrait MIPI-DSI JD9365-family panel (2 lanes),
GSL3670 I2C capacitive touch, PCA9535 IO expander for power sequencing, one
physical button, microSD (plain SPI).

**Partially bench-tested.** Confirmed working on real hardware: display,
touch, microSD (SPI mode), WiFi (ESP-Hosted backend), button. Every
pin/timing below still comes from Seeed's own ESP-IDF BSP
(`esp32_p4_re_terminal_d1001.h`/`.c`, checked out locally at
`D:\GitHub\reTerminal-D1001`) unless a note says otherwise. Anything marked
"unconfirmed" is a first guess carried over from that BSP and may need
correcting.

**Known hardware-specific requirement**: this board's flash chip is not on
Espressif's SPI-flash auto-suspend support list -- stock libs hit a hard
boot assert (`spi_flash: Suspend and resume may not supported for this
flash model yet.` / `__esp_system_init_fn_init_flash`). Confirmed fixed with
`CONFIG_SPI_FLASH_AUTO_SUSPEND=n` baked into a custom
`framework-arduinoespressif32-libs` build (tested against a local
`esp32p4_es2` chip variant). PlatformIO's `custom_sdkconfig` hybrid-compile
escape hatch does NOT work in this repo (conflicts with the root
`platformio.ini`'s own pinned custom libs package) -- this needs a dedicated
myLibBuilder release for this board, pinned via `platform_packages` the same
way `m5stack-tab5/platformio.ini` pins its own build. See the comment block
in `platformio.ini`.

## Power sequencing -- PCA9535 IO expander (I2C bus 1: SDA=20, SCL=21, addr 0x20)

This board **does not stay powered on its own** -- `BSP_PWR_HOLD` (expander
port1 bit0) has to be driven HIGH by software immediately at boot or the
board loses its own 3V3 rail, confirmed by reading `bsp_power_init()` in
full (it's the very first thing that function does after bringing up I2C).
`interface.cpp`'s `_setup_gpio()` does this before anything else.

| Function              | Expander bit | Port/bit  | Notes |
| ---                   | :---:        | :---:     | --- |
| Power hold (`PWR_HOLD`)| 8            | port1.0   | Must be HIGH to stay powered; dropped in `powerOff()`. |
| LCD power enable       | 0            | port0.0   | Panel's own power rail. |
| LCD backlight enable   | 7            | port0.7   | Gates the boost converter feeding `TFT_BL` (GPIO14 PWM); must be on before PWM does anything. |
| Battery read enable    | 6            | port0.6   | Gates the ADC divider on `ANALOG_BAT_PIN`. |
| Battery charge enable  | 10           | port1.2   | Active LOW (0 = charging enabled) -- left at 0. |
| LCD reset              | 2            | port0.2   | **Not used** -- `TFT_RST=-1` (software reset) instead, see below. |
| Camera/LTE bits        | 1,3,4,9,11-15| --        | Out of scope for this port, left as default input. |

Only these five bits are configured as outputs; everything else on the
expander is left at its power-on-reset default (input), per CLAUDE.md's
"no speculative flexibility" -- there's no camera/LTE support in this
launcher build.

The expander driver in `interface.cpp` (`_pcaWriteReg()`/`_pcaPowerUp()`) is
a minimal inline PCA9535 register write (Output0=0x02, Output1=0x03,
Config0=0x06, Config1=0x07, addr 0x20 = `ESP_IO_EXPANDER_I2C_PCA9535_ADDRESS_000`)
-- there is no PCA9535 support anywhere in `src/hal`, and this board is the
only reason to add one, so it stays board-local rather than becoming a new
HAL module.

## Display -- JD9365-family DSI panel, 800x1280, 2 lanes

| Signal          | Value |
| ---             | :---: |
| Databus         | `Arduino_ESP32DSIPanel` (`TFT_DATABUS_N=4`) |
| Driver          | `Arduino_DSI_Display` (`TFT_DISPLAY_DRIVER_N=50`) |
| Backlight (PWM) | GPIO14, gated by the expander's `LCD backlight enable` bit above |
| Reset           | `TFT_RST=-1` (software `SWRESET`) -- the BSP toggles reset through the expander's `BSP_LCD_RST` bit, but `lilygo-t-display-p4` already runs the same `Arduino_ESP32DSIPanel`/`Arduino_DSI_Display` combo with `TFT_RST=-1` successfully, so that's used here too rather than adding an expander dependency just for display bring-up. **Unconfirmed** this panel's own JD9365 core accepts a pure `SWRESET` the same way -- if the display never comes up, wiring `TFT_RST` through the expander (mirroring how `_pcaPowerUp()` already handles the other bits) is the first thing to try. |
| Init table      | `jd9365_8_800_1280_init_operations`, added to `lib/Arduino_GFX/src/display/Arduino_DSI_Display.h` next to the pre-existing, unrelated `jd9365_init_operations` array (different panel/gamma table, left untouched) |
| DPI timing      | h_size=800 v_size=1280, hsync: back=20 pulse=20 front=40, vsync: back=30 pulse=4 front=30, 60MHz DPI clock -- from Espressif's `JD9365_8_800_1280_PANEL_60HZ_DPI_CONFIG` macro |
| Lane rate       | 1000 Mbps, 2 lanes (`TFT_DSI_LANE_BIT_RATE=1000`) |

Rotation is set to `ROTATION=1` (native panel is portrait, UI wants
landscape) -- **unconfirmed** on real hardware; if the boot screen comes up
sideways or mirrored, try `ROTATION=3` first (touch's screen mapping in
`InputHandler()` currently only accounts for rotation 1 and will need
updating to match).

## Touch -- GSL3670, I2C, board-local driver

**Deliberately not wired into `src/hal/inputs/touch.cpp`'s `TOUCH_CTRL_*`
family** -- this repo's touch HAL has no GSL3670 case, and per the porting
brief this one stays board-local (`gsl3670_touch.h/.cpp`,
`gsl3670_fw_data.h`, `gsl_point_id.h/.cpp`).

**Confirmed on real hardware, and confirmed root cause of an initial "touch
doesn't work at all" failure**: `gsl3670_touch.cpp` uses `Wire1`, not the
default `Wire`. This chip sits on `BSP_I2C_0` (GPIO37/38), a different
physical I2C peripheral from the PCA9535 IO expander's `BSP_I2C_1` bus
(GPIO20/21, plain `Wire`, see `interface.cpp`). Both originally shared one
`TwoWire` object via repeated `Wire.begin()` calls, which silently
reassigns its pins out from under whichever peripheral configured it first
-- the exact same collision `lilygo-t-display-p4/interface.cpp` already
hit and documents (its "T-Display-P4 Keyboard add-on" comment, same fix:
give the second device its own `Wire1`).

| Signal | GPIO |
| ---    | :---: |
| SDA    | 37 (I2C bus 0, `BSP_I2C_0`) |
| SCL    | 38 |
| RST    | **PCA9535 expander bit 12** (`BSP_LCD_TOUCH_RST`, active LOW), driven from `interface.cpp`'s `d1001TouchResetSet()` via a callback passed into `gsl3670_init()` -- see note below |
| INT    | not used -- the BSP's own `bsp_touch_new()` sets `int_gpio_num = GPIO_NUM_NC` and polls instead; this port polls too, from `InputHandler()` |
| I2C addr | 0x40 |

**Reset pin note -- this was the actual root cause of touch never surviving
a cold boot** (I2C kept ACKing throughout, but the post-upload firmware
handshake at register 0xb0 always read back `00000000` instead of
`5A5A5A5A`). This port originally drove **raw GPIO12** as touch reset,
reasoning (wrongly) that `bsp_touch_new()`'s own reset block being commented
out meant reset happened via a plain GPIO. It doesn't: the BSP's real
`esp_lcd_touch_gsl3670.c` driver (`esp_lcd_touch_gsl3670_clear_reg()` /
`touch_gsl3670_reset()`, which run automatically inside
`esp_lcd_touch_new_i2c_gsl3670()`, independent of `bsp_touch_new()`'s own
commented-out block) always resets through the IO expander --
`esp_io_expander_set_level(io_expander, 1 << config.rst_gpio_num, level)` --
and `rst_gpio_num = 12` there is being reused as an **expander bit index**,
not a GPIO number. Worse: GPIO12 on the bare P4 is `BSP_WIFI_BOOT`, the
ESP32-C6 co-processor's boot-strap pin -- a completely unrelated signal.
Driving it as "touch reset" never touched the touch chip at all (explaining
why I2C to it stayed alive throughout) and risked disturbing the C6's own
boot strapping. Fixed to go through the expander bit, matching the BSP.

Orientation: BSP touch config is `x_max=800 y_max=1280 mirror_x=1
mirror_y=1 swap_xy=0` -- `gsl3670_touch.cpp` mirrors both axes before
returning a point, and `interface.cpp`'s `InputHandler()` then remaps the
resulting native (800x1280) point into screen space with the same
per-rotation table `lilygo-t-display-p4` uses for its own portrait-native
DSI panel (`rotation` switch: 0 identity, 1 `(ny, W-1-nx)`, 2 full mirror, 3
`(H-1-ny, nx)`). **Confirmed on real hardware for rotation 1** (perfectly
aligned) **and rotation 3** (both axes mirrored versus rotation 1, exactly
as the table predicts -- 3 is 1 rotated 180 deg). Rotations 0/2 are not
independently confirmed on this panel (nobody has driven the UI itself at
those rotations yet), but follow from the same table validated at 1 and 3,
not a separate guess -- if touches land in the wrong place or axis, this is
revisit, alongside the `ROTATION` note above.

The GSL3670 needs its internal-MCU firmware image uploaded over I2C on
every power-up (`gsl3670_fw_data.h`'s `gsl3670_fw[]`, ~4500 register writes)
plus a config blob (`gsl3670_config_data[]`) consumed by the ported
point-tracking algorithm (`gsl_point_id.cpp`, Silead's "no-ID" multitouch
resolver). Both blobs are vendored verbatim (opaque binary data, not driver
logic) from Seeed's BSP, Apache-2.0/MIT licensed like the rest of that BSP.
`gsl_point_id.cpp/.h` itself is copied close to verbatim from the same BSP
but carries Silead's own original GPL-2.0(-or-later) header -- left intact
at the top of that file.

## WiFi/BT -- ESP32-C6 co-processor over SDIO

| Signal        | GPIO |
| ---           | :---: |
| CLK           | 11   |
| CMD           | 6    |
| D0            | 7    |
| D1            | 8    |
| D2            | 9    |
| D3            | 10   |
| C6 enable/reset (`BSP_WIFI_CHIP_PU`) | 13 |

Built with `ENABLE_ESP_AT_INTERFACE=1` and the `SDIO2_*` pin macros, same
pattern as `lilygo-t-display-p4`: `launcherWifiInitSdioAuto()`
(`src/idf/idf_wifi.cpp`) tries ESP-AT-over-SDIO first and falls back to
ESP-Hosted-over-SDIO on its own -- no board-specific WiFi logic needed.

**Confirmed on real hardware**: WiFi comes up via the ESP-Hosted backend
(the C6 on this unit ships ESP-Hosted firmware, not AT). At boot you'll see
a burst of `sdmmc_io: sdmmc_io_rw_extended: sdmmc_send_cmd returned 0x107`
right after `[wifi-at] SDIO card initialized, max_freq=400 kHz` -- that is
`launcherWifiInitSdioAuto()`'s ESP-AT probe failing (as expected, since no
AT firmware is present) before it falls back to Hosted; benign noise, not a
bug. If a C6 unit with AT firmware is ever tested, confirm that path too.
If bring-up shows CRC/clock errors on this SDIO wiring the way
`elecrow-esp32p4-7in` needed (`support_files/patch_hosted_sdio_freq.py` /
`LAUNCHER_HOSTED_SDIO_FREQ_KHZ`), that same workaround is the fix -- it has
not been needed so far here.

## microSD -- plain SPI, NOT SD_MMC (SDMMC-slot-0 IOMUX pins reused as SPI signals)

ESP-Hosted has a known bug where SDIO1 (this SD card, if run as 4-bit
SD_MMC) and SDIO2 (the WiFi/C6 co-processor, above) cannot run concurrently
on the P4. `lilygo-t-display-p4` hits the exact same conflict on the same
chip, and uses the same fix applied here: the SD card stays on plain SPI
(`SD`/`SPIClass`) instead of `SD_MMC`, even though the pins are the same
ones the P4's SDMMC-slot-0 IOMUX exposes. `USE_SD_MMC` is deliberately not
set in `platformio.ini`.

| Signal     | GPIO | SPI role |
| ---        | :---: | :---: |
| CLK        | 43   | SCLK |
| CMD        | 44   | MOSI |
| D0         | 39   | MISO |
| D3         | 42   | CS |
| D1, D2     | 40, 41 | unused in SPI mode |
| Power EN (`BSP_SD_PWR_EN`) | 46 (plain GPIO, not the expander) |
| Detect (`BSP_SD_DETECT`)   | 45 (input, not currently polled -- the BSP uses it for auto mount/unmount on insert/remove; this port just power-cycles the rail once at boot and lets the normal SD mount path in `sd_functions.cpp` handle the rest) |

Power-enable polarity is confirmed active-HIGH against the BSP
(`gpio_set_level(BSP_SD_PWR_EN, 0)` then `1` immediately before
`bsp_sdcard_mount()`). `BOARD_SDMMC_POWER_CHANNEL=4` still applies even in
SPI mode -- the SDMMC-slot-0 pins sit in an IO domain fed by an on-chip LDO
regardless of which protocol drives them, but the plain `SD`/`SPIClass`
path (unlike `SD_MMC.begin()`) doesn't power it itself, so
`interface.cpp`'s `_powerSdCardIoLdo()` does it by hand once at boot (same
pattern as `lilygo-t-display-p4`). The SD rail switch (`SD_PWR_EN`) is also
power-cycled across `reboot()`/`powerOff()`, not just left running -- a
reset alone doesn't power-cycle the card, and leaving it powered mid-session
across a reset can leave it refusing to reinitialize for whatever firmware
boots next.

## Battery

| Signal          | GPIO |
| ---             | :---: |
| ADC (raw)       | 18 (ADC1 channel 2) |
| USB-present detect | 17 (ADC1 channel 1, not wired up in this port) |
| Read enable     | PCA9535 port0 bit6 (`BAT_READ_EN`), gated on once at boot |

`getBattery()` is not overridden -- the weak default in `src/mykeyboard.cpp`
reads `ANALOG_BAT_PIN` (18). Its default divider multiplier is **unconfirmed**
for this board's actual resistor divider (the BSP header doesn't state the
ratio); if the reported percentage is obviously wrong, override
`ANALOG_BAT_MULTIPLIER` once the real divider is known.

## Button

Single physical button (`HAS_1_BUTTON`, `hal_buttons_poll_1`):

| Button | GPIO |
| ---    | :---: |
| Sel    | 3    |

Short/double/long pulses all map off this one pin per `hal_buttons_poll_1`'s
standard convention (see `src/hal/README.md`).

**Confirmed on real hardware: this button is active-HIGH** with its own
external pull-down, not the HAL's default active-LOW/internal-pull-up
assumption (it read as permanently "pressed" until this was fixed). Fixed by
adding `DeviceButtons::activeHigh` (`src/hal/device.h`,
`src/hal/inputs/buttons.cpp`) -- a shared HAL change, since any future board
with an active-HIGH button needs the same flag. `buttonsCfg()` in
`interface.cpp` sets `pullup=false, activeHigh=true`.

## Not yet wired up / unknown

- LCD reset via the expander bit instead of `TFT_RST=-1`, if software reset
  turns out insufficient (see Display section) -- moot for now: display is
  confirmed working with `TFT_RST=-1`.
- Rotation direction/touch axis mapping beyond `ROTATION=1` (see Display
  and Touch sections) -- confirmed working as shipped, not revisited.
- Battery divider multiplier.
- SD card detect (GPIO45) is wired but not polled for hot-plug.
- Camera and LTE modem hardware on this board are out of scope for this
  launcher port (no camera/modem support in this codebase) -- their
  expander bits are left untouched.
- `powerOff()` doesn't arm a wakeup source before deep sleep -- the P4 has
  no `ext0` wakeup (RTC IO wakeup on this chip is `ext1`-only), and dropping
  `PWR_HOLD` is expected to cut the board's power outright rather than need
  a wakeup source at all. Unconfirmed on hardware; if the board doesn't
  power back on from the physical button afterwards, this is the first
  thing to revisit.
