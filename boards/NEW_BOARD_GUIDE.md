# Porting to a new board

This guide walks through adding a new device env to the Launcher. Read
[`src/hal/README.md`](../src/hal/README.md) first — most of the work below
is filling in structs and flags the shared HAL already implements, not
writing new drivers.

## 1. Naming the env

Env names follow `<vendor>-<device>{-<variant>}`, all lowercase,
hyphen-separated:

- `<vendor>` — the manufacturer or brand (`lilygo`, `m5stack`, `elecrow`,
  `xteink`, ...).
- `<device>` — the product line (`t-deck`, `cardputer`, `tab5`, ...).
- `<variant>` — optional, only when the same device line ships in more
  than one hardware variant that needs its own env (`-plus`, `-pro`,
  `-amoled`).

Examples: `lilygo-t-display-s3-amoled-plus`, `m5stack-cardputer`,
`elecrow-esp32s3-5in`. This convention applies to **new** boards; a number
of existing envs predate it (`CYD-*`, `Marauder-v4-OG`, `Awok-Touch`, ...)
and are not being renamed.

## 2. Create the board folder

```
boards/<new-env>/
  platformio.ini   # [env:<new-env>] section -- pins, chip selection, build_flags
  interface.cpp     # _setup_gpio, InputHandler, setBrightness, powerOff, reboot, getBattery
  connections.md     # pinout / bus-sharing / bring-up notes for humans
```

Copy the three files from [`boards/_New-Device-Model/`](_New-Device-Model)
as your starting point — it's a working template with every HAL hookup
commented inline, not a placeholder to guess at. `[env:_New-Device-Model]`
becomes `[env:<new-env>]` in your copy's `platformio.ini`; the folder name
and the `[env:...]` name must match.

`boards/_jsonfiles/` has one generic PlatformIO board JSON per MCU
(`esp32`, `esp32s2`, `esp32s3`, `esp32c3`, `esp32c5`, `esp32c6`, `esp32p4`,
`esp32p4_es`). Pick the one matching your chip as `board = ...` and
override anything that differs (flash size, PSRAM memory type, upload
speed) directly in your `platformio.ini`, same as the template shows.

## 3. Wire up inputs and power through the HAL

For each piece of hardware your board has, set the matching build flag in
`platformio.ini` and fill in the corresponding `Device*` struct in
`interface.cpp` — see `src/hal/README.md` for the full flag/API reference
per module (buttons, encoder, touch, PMIC, gauge, backlight). Do **not**
write a new driver for hardware the HAL already covers (any `TOUCH_CTRL_*`
family, `PMIC_BQ25896`, `GAUGE_BQ27220`, plain-PWM backlight, raw-GPIO
buttons/encoder) — call the shared `hal_*` functions instead.

Only fall back to board-specific code in `interface.cpp` when the hardware
genuinely isn't covered: a keyboard (always board-specific, see
`src/hal/README.md`), a PMIC/gauge chip the HAL doesn't implement yet
(AXP2101, AXP192, SY6970, MAX17048, ...), or a higher-level abstraction
like M5Unified that already owns input/power/brightness for that board.

## 4. Register the env

Add the new env in both places below, **commented out** (`;env-name`) and
in alphabetical order among the existing entries — this keeps CI from
building it until it's actually ready, and keeps both lists scannable.

1. `platformio.ini` (repo root), inside `[platformio]` → `default_envs`.
2. `.github/workflows/main.yml`, inside `compile_sketch.strategy.matrix.board`
   as `- { env: "<new-env>" }`.

When the board is confirmed working (build passes and, ideally, tested on
real hardware), uncomment both lines in the same PR or a follow-up.

## 5. Build

```
platformio run -e <new-env>
```

Fix compile errors before anything else — pin/macro typos, a missing
`lib_deps` entry for a chip-specific library (`RotaryEncoder` for
`HAS_ENCODER`, `ESP32_Button` for `BUTTONS_IDF_COMPONENT`), or a flag left
on that your board's chip doesn't support.

## 6. Document the pinout

Fill in `boards/<new-env>/connections.md` from the template: which pins
each peripheral uses, which buses are shared (SPI/I2C) and by what,
required GPIO states at boot (e.g. a pin that must be held high to stay on
battery power), and anything else the next person porting a similar board
would want to know. This is for humans reading the board folder later, not
generated from the code — keep it accurate as pins change.

## 7. Test on hardware

Flash the board and walk the golden path: boot, navigate the menu with
every input the board has (buttons/encoder/touch), check battery
percentage and charging state if the board has a gauge/PMIC, check
brightness control, check power off and reboot. `USE_DUMMY_TFT=1` and
`HEADLESS=1` (see `platformio.ini`'s template comments) are available for
bring-up before a display driver is confirmed working.

## Reference implementations

When your board's hardware combination isn't obvious from the template
alone, find an already-migrated board using the same chip and read its
`interface.cpp`/`platformio.ini` — `src/hal/README.md`'s per-module tables
name which `TOUCH_CTRL_*`/`PMIC_*`/`GAUGE_*` value maps to which chip, and
`boards/<env>/platformio.ini` across the repo is searchable by flag.
