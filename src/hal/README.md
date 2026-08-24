# src/hal

Shared hardware-abstraction code, factored out of `boards/<env>/interface.cpp`
duplication. See `docs/plan.md` for the migration plan this follows.

## Layout

- `inputs/buttons.*` — GPIO button polling (HAS_1_BUTTON..HAS_6_BUTTONS)
- `inputs/touch.*` — touchscreen drivers (XPT2046, GT911, CST8xx, FT6X36,
  GT9895, HI8561 — the FT6X36 driver covers the whole FT6206/FT6236/
  FT6336(U)/FT3267/FT5336/FT3068 family, there's no separate "FT6336"
  driver)
- `inputs/encoder.*` — rotary encoder + associated select/esc buttons
- `inputs/keyboard.*` — **does not exist.** Etapa 6 (keyboard unification)
  was skipped at the author's decision: TCA8418/I2C-custom/GPIO-matrix
  keyboards each work differently enough that a shared HAL wasn't worth it
  — keyboard reading stays board-specific in each `interface.cpp`
  (`lilygo-t-lora-pager`, `lilygo-t-deck`, `m5stack-cardputer`, etc.), see
  `docs/etapa_6.md`.
- `power/pmic.*` — charger IC init (BQ25896, AXP2101, AXP192, SY6970).
  `hal_pmic_get_ntc_percent()` (Etapa 7) is a `PMIC_BQ25896`-only
  passthrough to `getNTCPercentage()`, for a board with no separate fuel
  gauge that estimates battery% from the charger's own NTC reading instead
  of `hal_pmic_get_system_voltage_mv()` (`lilygo-t-display-s3-amoled-plus`,
  migrated off a direct `PowersBQ25896` instance this etapa — see
  `docs/etapa_7.md`). All other `PMIC_BQ25896` boards
  (`lilygo-t-embed-cc1101`, `lilygo-t-lora-pager`, `reaper`,
  `smoochiee-board`, `lilygo-t-deck-pro`, `lilygo-t5-epaper-s3-pro`) were
  already on `hal_pmic_*` from Etapa 1.
- `power/gauge.*` — fuel gauge IC (BQ27220, MAX17048)
- `bright/bright.*` — backlight PWM curve
- `device.h` — pure-data structs (`DeviceButtons`, `DeviceTouch`,
  `DeviceEncoder`, `DevicePmic`, `DeviceGauge`) a board fills in
  `_setup_gpio()` and hands to the `hal_*` functions below.

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
    `CYD-2432W328R`, `CYD-3248S035R`, `CYD-4827S043R`, `ESP32-E32R40T`),
    `lilygo-t-hmi`, `elecrow` (`-24B`/`-28B`/`-35B`/`-35Bv2_2`),
    `marauder-v4og` (`Marauder-v4-OG`/`Marauder-v61`/`Awok-Touch`/
    `WaveSentry-R1`), `marauder-v8`, `NM-CYD-C5`.
  - `TOUCH_CTRL_GT911`: `lilygo-t-deck`/`lilygo-t-deck-plus`,
    `elecrow-esp32p4-7in`, `elecrow-esp32s3-5in`, `lilygo-t5-epaper-s3-pro`
    (touch controller lives on `EPD_Painter`'s own `TwoWire` —
    `DeviceTouch.i2c_bus` set from `tft->getConfig().i2c.wire`, two
    hardware variants with different reset/irq pins picked at runtime),
    `xteink-x4pro` (home key is a GT911 hardware key bit, not a coordinate
    zone — see `hal_touch_set_home_button()` below), `seeedstudio-
    reterminal-sticky` (register map confirmed identical to the
    hand-rolled I2C driver it replaced before migrating — see
    `docs/etapa_7.md`; goes through `hal_touch_read_raw()` +
    `hal_touch_get_resolution()` instead of `hal_touch_read()`, because
    this panel's native GT911 touch resolution doesn't match its pixel
    dimensions and `setSwapXY()`+`setTargetResolution()` combined scale the
    wrong axis by the wrong factor on a non-square native resolution — the
    board keeps its own proven per-rotation rescale math, just fed by the
    HAL's I2C/reset/probe instead of a hand-rolled register driver),
    `CYD-2432S032C`/`CYD-3248S035C`/`CYD-8048S043C`/`CYD-8048W550C`
    (`boards/CYD-2432S028/interface.cpp`, shared with the rest of the CYD
    family — replaced a `TouchLib`-based wrapper; standard `hal_touch_read()`
    + `cfg.SwapXY`/`MirrorX`/`MirrorY`, same as every other `TOUCH_CTRL_GT911`
    board — the per-rotation table was solved algebraically by composing
    the old wrapper's fixed pre-transform with `InputHandler()`'s old
    shared per-rotation remap block into one table, see `docs/etapa_7.md`).
    All build-only so far (Etapa 7, see `docs/etapa_7.md`).
  - `hal_touch_read_raw()` / `hal_touch_get_resolution()` (Etapa 7): a raw
    escape hatch alongside `hal_touch_read()` for `TOUCH_CTRL_GT911`/
    `_CST8XX`/`_FT6X36`/`_GT9895`/`_HI8561` — returns the point exactly as
    the driver reports it (no `cfg.SwapXY`/`MirrorX`/`MirrorY`, no
    resolution scaling) plus the controller's own reported native
    resolution, for a board whose rotation/scale math doesn't fit
    `hal_touch_read()`'s model (see `seeedstudio-reterminal-sticky` above).
  - `TOUCH_CTRL_GT9895` / `TOUCH_CTRL_HI8561` (Etapa 7): added for
    `lilygo-t-display-p4`'s two panel variants (GT9895 on AMOLED, HI8561 on
    IPS, chosen at runtime), but **`lilygo-t-display-p4` itself was
    deliberately left unmigrated** — its touch reset/irq are routed through
    an IO-expander GPIO callback (`TouchDrvInterface::setGpioCallback()`),
    and it picks between two driver *types* at runtime via a
    `TouchDrvInterface*`, neither of which `DeviceTouch`/`hal_touch_init()`
    support (one fixed compile-time driver per macro, plain `int8_t` GPIO
    pins only). Forcing that through the shared HAL would mean extending
    `DeviceTouch` for a single board's benefit at real risk to every other
    `TOUCH_CTRL_*` board. The drivers exist in the HAL (compiles clean,
    verified with a forced `-DTOUCH_CTRL_GT9895`/`-DTOUCH_CTRL_HI8561`
    build against a board with `SensorLib` available) so a future board
    that actually fits the HAL's single-driver, direct-GPIO model can use
    them — `lilygo-t-display-p4` isn't that board, see `docs/etapa_7.md`.
    `TOUCH_CTRL_GT9895` needs `DeviceTouch.raw_width`/`raw_height` set
    (this chip doesn't report its native resolution over I2C, unlike GT911
    — a fixed vendor/panel constant the board must supply, e.g. 1060x2400
    on the T-Display-P4's AMOLED); `TOUCH_CTRL_HI8561` already reports in
    panel coordinates, no scaling needed.
  - `TOUCH_CTRL_CST8XX`: `lilygo-t-display-s3-touch` (touch only —
    its 2 physical buttons stay on their own raw `Button`/`ESP32_Button`
    code, out of scope for Etapa 4), `lilygo-t-display-s3-amoled`,
    `lilygo-t-display-s3-amoled-plus`, `lilygo-t-display-s3-pro`,
    `lilygo-t-watch-ultra` (`TouchDrvCST92xx`, reset routed through an IO
    expander — `cfg.pin_rst = -1`, pulsed by hand before `hal_touch_init()`,
    same pattern as `lilygo-t-deck-pro`'s MAX variant below),
    `lilygo-t-deck-pro` (3 hardware variants detected at runtime, each with
    its own reset pin or none at all for the MAX variant),
    `CYD-2432S022C`/`CYD-2432S022C-lovyan`/`CYD-2432W328C`/
    `CYD-2432W328C_2` (CST820, same `TouchDrvCST816`-family auto-detection
    as CST816S — register map confirmed identical before migrating; same
    `hal_touch_read()` + swap/mirror table as the GT911 CYD envs above, a
    1px fencepost difference from the original driver dropped on purpose —
    see `docs/etapa_7.md`) — all confirmed on physical hardware except
    `-amoled`/`-amoled-plus`/`-pro`/`-watch-ultra`/`-deck-pro`/the CYD ones
    (build-only so far, see `docs/etapa_4.md`/`docs/etapa_7.md`).
  - `hal_touch_set_home_button()` works for `TOUCH_CTRL_GT911` too, not
    just `TOUCH_CTRL_CST8XX` — the underlying SensorLib callback lives on
    the shared `TouchDrvInterface` base, and `TouchDrvGT911` fires it off a
    hardware key bit rather than proximity to `setCenterButtonCoordinate()`
    (which GT911 ignores, so `xteink-x4pro` just passes `0, 0`).
  - `TOUCH_CTRL_FT6X36`: `lilygo-t-watch-s3` (SensorLib `TouchDrvFT6X36`,
    on a second I2C bus — see `cfg.i2c_bus` below; confirmed physically),
    `ES3C28P`, `pancake` (build-only so far — both originally had a
    byte-identical hand-rolled I2C driver instead of SensorLib; migrated to
    `TouchDrvFT6X36` after confirming register-for-register equivalence,
    see `docs/etapa_4.md`). `hal_touch_set_threshold()` is a
    `TOUCH_CTRL_FT6X36`-only passthrough for boards that need a
    non-default touch-sensitivity register (`ES3C28P`/`pancake` do,
    `lilygo-t-watch-s3` doesn't).
  - `settings.cpp`'s touch calibration screen (`HAS_RESISTIVE_TOUCH`)
    still reaches the XPT2046 driver via `extern CYD28_TouchR touch;`,
    which `hal/inputs/touch.cpp` now defines with external linkage
    precisely so that keeps working unchanged, both for migrated and
    not-yet-migrated boards.
  - `DeviceTouch.i2c_bus` (`void*`, cast to `TwoWire*` inside
    `hal/inputs/touch.cpp`, nullptr = the global `Wire`) is for a board
    whose touch controller sits on a second I2C bus, e.g.
    `lilygo-t-watch-s3`'s `Wire1`. `void*` rather than `TwoWire*` so
    `device.h` — included by every board regardless of what sensors it
    has — stays Arduino-free.
- `inputs/encoder.*` (Etapa 5) -- `HAS_ENCODER`, `RotaryEncoder` lib, all
  migrated boards use `hal_encoder_init`/`hal_encoder_poll`:
  `lilygo-t-embed-cc1101` (both variants, `pin_esc=-1` on the plain one
  since it has no back button), `m5stack-dinmeter`, `marauder-v4og`'s
  `WaveSentry-R1` env (all three `LatchMode::TWO03`), and
  `lilygo-t-lora-pager` (`LatchMode::FOUR3`, encoder entangled with the
  TCA8418 keyboard in the same `InputHandler()` -- `encoderCfg()` swaps
  `pin_a`/`pin_b` to correct a Next/Prev direction inverted relative to the
  other three boards, since `hal_encoder_poll()` itself has no invert flag;
  see `docs/etapa_5.md`). `encoder.cpp` is guarded end-to-end by `#if
  defined(HAS_ENCODER)` (stubs otherwise) so PlatformIO's LDF doesn't need
  `RotaryEncoder` in `lib_deps` for boards without it.
  - Not migrated: `CYD-2432S028`'s `TOUCH_AXS15231B_I2C` branch
    (`CYD-3248W535C`) — `bb_captouch`, no SensorLib driver for this chip
    exists. Its GT911/CST816S(CST820) branches migrated to
    `TOUCH_CTRL_GT911`/`TOUCH_CTRL_CST8XX` in Etapa 7 (see above and
    `docs/etapa_7.md`); `TouchLib` was dropped from
    `boards/CYD-2432S028/platformio.ini`'s `lib_deps` entirely once nothing
    referenced it anymore.
- `inputs/buttons.*`'s `hal_buttons_init_2`/`hal_buttons_poll_2` (Etapa 7):
  double-click now also raises Sel (btn1)/Esc (btn2), same as a long press —
  added for `lilygo-t-display-s3-touch`, which used both triggers for the
  same action; additive, no behavior change for `lilygo-t-display-c5`/
  `m5stack-sticks3` (never used double-click). See `docs/etapa_7.md`.
