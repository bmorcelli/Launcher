Waveshare ESP32-P4-WIFI6-Touch-LCD-5 -- ESP32-P4 (P4NRW32X: 32MB PSRAM
stacked in-package, 32MB external NOR flash) main SoC + ESP32-C6-MINI-1
co-processor (SDIO, WiFi 6/BLE 5), 720x1280 portrait MIPI-DSI HX8394 panel
(2 lanes), GT911 I2C capacitive touch, microSD (SDMMC 4-bit, slot 0),
ES8311/ES7210 audio codec (unused by this port), CH343 USB-serial console on
one USB-C and a second USB-C wired to the P4's own USB 2.0 high-speed OTG
pads.

**Bench-tested** on a P4 rev v1.3 unit: display, GT911 touch, microSD, the
ESP32-C6 WiFi bridge and installing an app from SD into the OTA slot all
work. Every pin and timing below started from Waveshare's own schematic,
audited BSP and docs (waveshareteam/ESP32-P4-WIFI6-Touch-LCD-5: the
schematic PDF, docs/IO.md, the esp_lcd_hx8394 driver, and
examples/arduino/libraries/displays/displays_config.h); anything still
marked "unconfirmed" was not exercised on hardware, and the audio codec is
unused by this port.

## Flash, PSRAM and partitions

- 32MB external NOR flash; 32MB in-package PSRAM (`BOARD_HAS_PSRAM`), same
  P4NRW32X silicon the other P4 boards here run on.
- The shared `boards/_jsonfiles/esp32p4_es.json` defaults to a 16MB flash
  declaration, so `platformio.ini` sets `board_upload.flash_size`/
  `maximum_size` to 32MB explicitly. At runtime the launcher detects the
  real chip size with `esp_flash_get_size()` (`partition_table_model.cpp`)
  and lays its OTA/data slots out in the free flash past the static
  partitions -- the CSV only has to place the launcher's own slot.
- `support_files/custom_32Mb_p4.csv` (new file, sibling of the 16MB P4 one):
  `app0` subtype **factory** 0x210000 @ 0x10000, coredump @ 0x220000. On the
  P4, `partitioner.cpp` installs firmwares into
  `ESP_PARTITION_SUBTYPE_APP_FACTORY` (unlike the S3's TEST), so reusing the
  pre-existing `custom_32Mb.csv` (subtype `test`) would have left this board
  unable to install anything. Everything past 0x230000 is managed by the
  runtime partition model.

## Display -- HX8394 DSI panel, 720x1280, 2 lanes

| Signal                | Value |
| ---                   | :---: |
| Databus               | `Arduino_ESP32DSIPanel` (`TFT_DATABUS_N=4`) |
| Driver                | `Arduino_DSI_Display` (`TFT_DISPLAY_DRIVER_N=50`) |
| Reset                 | GPIO27 (`TFT_RST`, 0R R60 -> panel `RESET`) |
| Backlight PWM         | GPIO26 (`TFT_BL`) -- **AC-coupled**, see below |
| Backlight enable      | GPIO33 (`TFT_BL_EN`, AP3032 boost `EN` through 0R R32, active HIGH) |
| DPI clock             | 58 MHz, 30 Hz refresh |
| HSYNC pulse/back/front| 20 / 20 / 40 |
| VSYNC pulse/back/front| 4 / 10 / 24 |
| Lane rate             | 700 Mbps, 2 lanes (`TFT_DSI_LANE_BIT_RATE=700`) |

Timings are Waveshare's own `HX8394_720_1280_PANEL_30HZ_DPI_CONFIG`.

**Backlight is PWM-only**: Waveshare's displays_config.h documents that the
panel's backlight path AC-couples the PWM signal, so a static GPIO level
lights nothing -- the pin has to be driven by PWM. The shared `hal_bright`
HAL's 5 kHz ledc output satisfies this (Waveshare's own BSP drives 5
kHz/10-bit). `TFT_BL_EN` (boost enable) is driven HIGH once in
`_setup_gpio()` before the PWM starts -- unlike Elecrow's `TFT_BL_POWER`,
this one is active HIGH.

**Init table**: `waveshareHx8394InitOperations` in `interface.cpp`,
transcribed verbatim from Waveshare's official
`vendor_specific_init_default` (25 commands). `TFT_DSI_INIT` in
`platformio.ini` only names the stock Arduino_GFX `hx8394_init_operations`
array to satisfy the build-time seeding in `DisplayConfig.cpp` -- that
array belongs to a different panel revision (different power/gamma/VCOM/GIP
tables) and would likely not bring this glass up. `_setup_gpio()` re-aims
`displayConfig` at the Waveshare table via `TFT_SET_DSI_INIT()` before
`tft->begin()`, the same runtime-swap pattern `boards/lilygo-t-display-p4`
uses for its AMOLED variant. No Arduino_GFX fork/submodule change is
needed (unlike Elecrow, whose `ek79007` table only exists in a pinned
fork).

Rotation is set to `ROTATION=3` (native panel is portrait, UI wants
landscape), the same value the other portrait-glass P4/DSI boards here
settled on (`lilygo-t-display-p4`, `seeedstudio-reterminal-d1001`).
`ROTATION=3` comes up upright on a rev v1.3 unit; both 1 and 3 give a
landscape 1280x720 UI differing by a 180-degree flip, so a different panel
revision that shows the image upside-down needs `ROTATION=1` (touch follows
automatically via the per-rotation mirror table in `touchCfg()`).

## Touch -- GT911, I2C (shared board bus)

| Signal  | GPIO |
| ---     | :---: |
| SDA     | 7 (board I2C bus; ES8311/ES7210 codec hang off it too) |
| SCL     | 8 |
| RST     | 23 (0R R37) |
| INT     | 2 -- **only reaches the touch glass through optional 0R R108, not fitted by default** |
| I2C addr | 0x5D and 0x14 both legal (latched from INT at reset) |

The GT911 latches one of two I2C addresses from the INT pin level at reset.
On this board GPIO2 -> R108 -> touch INT, and R108 is an optional, by
default **not fitted** part, so the address can't be forced from the P4
side -- the chip floats to whatever its internal pull presents (0x5D by
convention). The port calls `hal_touch_init(touchCfg(), 0x5D)` once with
`gt911_int_sync=true`: that path pulses RST and performs the INT sync
dance (harmless when R108 is unfitted -- it drives a pad that goes
nowhere), then hands SensorLib stripped pins so `TouchDrvGT911`'s
`probeAddress()` tries both 0x5D and 0x14 on its own. Either latch state
ends up working, without the manual 0x5D->0x14 retry other boards carry.

Touch coordinates follow the launcher rotation via the standard
SwapXY/MirrorX/MirrorY table (same as Elecrow's).

RST (GPIO23) is driven through `DeviceTouch.reset_cb` with `pin_rst = -1`
instead of the plain `pin_rst` field. That makes `hal_touch_init()` pass -1
as the driver's own reset pin, so the `touch.reset()` that
`hal_touch_read()` runs after every reported point no longer pulses RST.
With a real pin_rst, `TouchDrvInterface::reset()` re-initialised the GT911
after each read and the still-down finger got re-latched as a new touch --
which made every on-screen keyboard key type twice. `InputHandler()` also
applies a point only on its press edge (hold >500 ms repeats at 250 ms),
following `seeedstudio-wio-tracker-l2`, so a finger resting on a key cannot
re-trigger it.

Note: GPIO2 is also broken out on the 40-pin HAT header (pin 8). If a HAT
drives it while R108 is fitted, it fights the touch INT line -- leave it
alone on this port.

## WiFi/BT -- ESP32-C6-MINI-1 over SDIO

| Signal                    | GPIO |
| ---                       | :---: |
| CLK                       | 18   |
| CMD                       | 19   |
| D0                        | 14   |
| D1                        | 15   |
| D2                        | 16   |
| D3                        | 17   |
| C6 enable/reset           | 54 (0R R34 -> C6 `EN`) |

Each SDIO line has its own 51K pull-up and the pin order matches
Espressif's reference wiring for a P4+C6 pair -- deliberately NOT the
reversed D0..D3 order `elecrow-esp32p4-7in`'s hardware revisions forced
(see that board's `interface.cpp` header). All pins can be overridden at
runtime from NVS (the `sdio` serial console command) rather than requiring
a rebuild, same mechanism as Elecrow's.

Built with the `SDIO2_*` macros and `launcherWifiInitSdioAuto()`
(`src/idf/idf_wifi.cpp`) -- ESP-Hosted firmware on the C6 (what Waveshare
ships). `ENABLE_ESP_AT_INTERFACE` is not set. If bring-up shows the
intermittent SDIO CRC errors Elecrow's wiring hit
(`patch_hosted_sdio_freq.py` / `LAUNCHER_HOSTED_SDIO_FREQ_KHZ=20000`), that
same workaround is the fix -- it has not been needed/known here yet.

GPIO6 also routes to `C6_IO2` through R33 (purpose unknown, ignore it).
The board carries dedicated 4-pin 2.54mm pads to reflash the C6's own
firmware (see Waveshare's "Firmware Flashing" docs page).

**If WiFi suddenly reports `ESP-Hosted unavailable: no WiFi`**: the hosted
bring-up takes ~8 s at boot, and the guard in `idf_wifi.cpp` latches
"co-processor broken" if the board loses power or is reset inside that
window (a normal power cycle right after boot is enough). It then skips
bring-up on every later boot until cleared. Fix from the serial console:

```
wifi hosted      # clears the latched verdict
```

then power-cycle the board so bring-up runs again. Do not use the `reboot`
command for this: it is a CPU reset, and the installed app (not the
Launcher) is what boots from it.

Not board-specific -- every ESP-Hosted port (Elecrow, LilyGO P4, ...)
behaves the same way. This board was hit by it once during bring-up
testing; after clearing, the C6 scanned/connected normally.

**Benign boot warning**: the C6's esp_hosted firmware reports its version as
`0.0.0`, so esp_hosted logs a version-mismatch and a `rpc_core: Response not
received for [0x15e](Req_GetCoprocessorFwVersion)` line at every boot. The
radio still comes up (1476 ms here) and connects; the same unit running
Espressif's own P4 demo firmware shows `coprocessor=0.0.0` / "the C6 did not
say its firmware version" and then gets a DHCP lease, so it is a co-processor
firmware reporting quirk, not a wiring or port problem.

## microSD -- SDMMC 4-bit, slot 0

| Signal     | GPIO |
| ---        | :---: |
| CLK        | 43   |
| CMD        | 44   |
| D0         | 39   |
| D1         | 40   |
| D2         | 41   |
| D3         | 42   |
| Power      | on-chip LDO VO4 (`BOARD_SDMMC_POWER_CHANNEL=4`) |

The slot is documented "SDIO 3.0" by Waveshare; this port drives it as
4-bit `SD_MMC` on the P4's dedicated SDMMC-slot-0 pads, same macro set as
`elecrow-esp32p4-7in` (`USE_SD_MMC=1`, slot 0).

**Checked on hardware**: ESP-Hosted has a known bug where SDIO1 (the SD
card in 4-bit SD_MMC mode) and SDIO2 (the C6) cannot run concurrently on
the P4 -- `lilygo-t-display-p4` and `seeedstudio-reterminal-d1001` avoid it
by driving the card over plain SPI on the same pins instead. On a rev v1.3
unit the card stays mounted while the C6 scans and connects, so the
conflict does not show up with this wiring; if another revision errors the
card out once WiFi is up (or vice versa), the documented fix is switching
to SPI mode like those two boards.

## USB

- H1 (silkscreen "USB TO UART", CH343): power + flashing + console. UART0
  on the P4's dedicated pins; `monitor_dtr=0`/`monitor_rts=0` so the
  monitor doesn't toggle the CH343's DTR/RTS and reset the board.
- H2 (silkscreen "USB OTG"): wired straight to the P4's dedicated
  `USBD_N`/`USBD_P` pads (chip pins 49/50, not on the GPIO matrix --
  confirmed by the docs' own 40-pin header note). Drives
  `USB_MSC_HIGH_SPEED=1` (massStorage.cpp's rhport 0 vs 1), same as
  `lilygo-t-display-p4`'s "High Speed USB" port.
- `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=0` (console is UART0 via
  the CH343, not USB-CDC).

Flashing for serial-command tests uses `esptool` directly (per CLAUDE.md).
ESP32-P4, so the bootloader goes at **0x2000** (the offset
`support_files/merge.py` maps for esp32p4):

```
esptool --chip esp32p4 write-flash \
  0x2000  .pio/build/waveshare-esp32-p4-wifi6-touch-lcd-5/bootloader.bin \
  0x8000  .pio/build/waveshare-esp32-p4-wifi6-touch-lcd-5/partitions.bin \
  0x10000 .pio/build/waveshare-esp32-p4-wifi6-touch-lcd-5/firmware.bin
```

The build also emits the same three files pre-merged with those offsets as
`Launcher-waveshare-esp32-p4-wifi6-touch-lcd-5.bin` -- flashing that single
image at 0x0 brings a bare board up in one shot.

Note on what each flash wipes: the Launcher's own settings (including the
WiFi list, passwords encrypted) live in `config.conf` on the microSD, so
they survive any flash. The image at 0x0 also overwrites the NVS partition
(0x9000-0xE000), which resets the ESP-Hosted guard and the NVS mirror of
those settings -- harmless, the SD copy is reloaded on boot. Flashing just
`firmware.bin` at 0x10000 leaves NVS untouched.

## Battery

MX1.25 2-pin header for a 3.7V LiPo (charge/discharge supported by the
board's own charger), but there is **no fuel gauge** and no documented ADC
divider -- `getBattery()` is not overridden and no battery icon is wired
in. If a divider turns out to exist on a future revision, override
`ANALOG_BAT_PIN`/`ANALOG_BAT_MULTIPLIER` then.

## Not wired up / out of scope

- ES8311 codec + ES7210 echo-cancellation + speaker amp: I2S on GPIO
  9 (`I2S_DSDIN`), 10 (`I2S_LRCK`), 11 (`I2S_ASDOUT`), 12 (`I2S_SCLK`),
  13 (`I2S_MCLK`), PA enable GPIO53. No audio path exists in this
  launcher; all left untouched.
- MIPI-CSI camera connector (OV5647) -- no camera support in this
  codebase.
- RTC battery header, speaker header, onboard mic array.
- 40-pin Raspberry-Pi-HAT header GPIOs (3, 4, 5, 21, 22, 28-32, 34, 35,
  37, 38, 46-52, plus GPIO24/25 as USB1P1 full-speed USB) -- untouched.
- POWER button (hard-wired 2s power-off) and BOOT button (strap/download
  mode) are board functions, not launcher inputs -- no `DeviceButtons`
  config, matching the other P4 display boards.
