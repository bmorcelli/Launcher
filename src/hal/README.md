# src/hal

Shared hardware-abstraction code, factored out of `boards/<env>/interface.cpp`
duplication. See `docs/plan.md` for the migration plan this follows.

## Layout

- `inputs/buttons.*` — GPIO button polling (HAS_1_BUTTON..HAS_6_BUTTONS)
- `inputs/touch.*` — touchscreen drivers (XPT2046, GT911, CST8xx, FT6336)
- `inputs/encoder.*` — rotary encoder + associated select/esc buttons
- `inputs/keyboard.*` — TCA8418, I2C-custom, and GPIO-matrix keyboards
- `power/pmic.*` — charger IC init (BQ25896, AXP2101, AXP192, SY6970)
- `power/gauge.*` — fuel gauge IC (BQ27220, MAX17048)
- `bright/bright.*` — backlight PWM curve
- `device.h` — pure-data structs (`DeviceButtons`, `DeviceTouch`,
  `DevicePmic`, `DeviceGauge`) a board fills in `_setup_gpio()` and hands to
  the `hal_*` functions below.

## Conventions

- Function naming: `hal_<module>_init(...)`, `hal_<module>_poll(...)` (or a
  more specific verb, e.g. `hal_gauge_get_percent()`).
- Chip/family selection is a `build_flags` macro set per board in
  `boards/<env>/platformio.ini`, mirroring the existing `HAS_*` convention
  (e.g. `-DPMIC_BQ25896`, `-DGAUGE_BQ27220`, `-DTOUCH_CTRL_GT911`,
  `-DKEYBOARD_TCA8418`).
- Every `hal_*.cpp` file compiles for **every** env by default (it lives
  under `src/`, which PlatformIO includes unconditionally). To avoid
  pulling in a chip-specific library for boards that don't need it, all
  library-specific `#include`s and code must sit behind the board's
  selection macro (`#ifdef PMIC_BQ25896 ... #endif`), never at file scope.
  PlatformIO's dependency finder respects these guards when deciding which
  libraries to link.
- A `hal_*` function must never depend on `<Arduino.h>` unless the
  underlying vendor library requires it (XPowersLib, bq27220, TCA8418,
  RotaryEncoder all do) — prefer `src/idf/launcher_platform.h` wrappers
  (`launcherGpioRead`, `launcherMillis`, ...) for anything else.
- Boards that fall outside the HAL (e.g. M5Unified-based touch/buttons,
  which already has its own abstraction) are documented here as they're
  identified, so they don't look forgotten:
  - `arduino-nesso-n1`, `m5stack-core2`, `m5stack-cores3`,
    `m5stack-paper-s3`, `m5stack-tab5` — touch via `M5.Touch`, out of scope
    for `inputs/touch.*`.
  - `arduino-nesso-n1`, `m5stack-core`, `m5stack-cplus1_1` — buttons via
    `M5Unified`/`AXP192`, out of scope for `inputs/buttons.*` (GPIO-only).
  - `m5stack-sticks3`, `lilygo-t-display-c5` — the two real "2 buttons"
    boards; both use `hal_buttons_init_2`/`hal_buttons_poll_2`
    (`BUTTONS_IDF_COMPONENT=1`), backed by the ESP-IDF `Button` component
    (interrupt callbacks) instead of raw GPIO polling like 1/3/5/6 buttons
    — see `docs/etapa_2.md`.
  - `TOUCH_CTRL_XPT2046`: `phantom`, `CYD-2432S028`-family default branch
    (`CYD-2432S028`, `CYD-2-USB`, `CYD-2432S024R`, `CYD-2432S032R`,
    `CYD-2432W328R`, `CYD-3248S035R`, `CYD-4827S043R`, `ESP32-E32R40T` —
    the GT911/CST816S/AXS15231B branches of that same shared
    `interface.cpp` are NOT migrated, see below), `lilygo-t-hmi`, `elecrow`
    (`-24B`/`-28B`/`-35B`/`-35Bv2_2`), `marauder-v4og`
    (`Marauder-v4-OG`/`Marauder-v61`/`Awok-Touch`/`WaveSentry-R1`),
    `marauder-v8`, `NM-CYD-C5`.
  - `TOUCH_CTRL_GT911`: `lilygo-t-deck`/`lilygo-t-deck-plus`,
    `elecrow-esp32p4-7in`, `elecrow-esp32s3-5in`.
  - `settings.cpp`'s touch calibration screen (`HAS_RESISTIVE_TOUCH`)
    still reaches the XPT2046 driver via `extern CYD28_TouchR touch;`,
    which `hal/inputs/touch.cpp` now defines with external linkage
    precisely so that keeps working unchanged, both for migrated and
    not-yet-migrated boards.
  - Not migrated, documented in `docs/etapa_3.md`: `CYD-2432S028`'s
    GT911/CST816S/AXS15231B branches (different lib, `TouchLib`/
    `bb_captouch`, not `SensorLib`), `lilygo-t5-epaper-s3-pro` (GT911 on a
    driver-owned `TwoWire`, not the global `Wire` the HAL assumes),
    `xteink-x4pro` and `seeedstudio-reterminal-sticky` (touch + buttons
    combo, Etapa 7).
