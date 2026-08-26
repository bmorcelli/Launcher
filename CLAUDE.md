# CLAUDE.md

Guidance for agents working in this repository — a multi-board ESP32
Arduino/PlatformIO firmware ("Launcher") that boots and manages other
firmware across dozens of hardware devices (LilyGO, M5Stack, CYD, Elecrow,
Marauder, and more).

## Flash-size discipline

Every supported board flashes into a fixed, often small partition. This
project is optimized for binary size first, not for abstraction or
"clean code" for its own sake:

- Reuse existing helpers instead of writing a new one that does almost the
  same thing. Before adding code, check `src/hal/**` (input polling,
  power ICs, backlight), `src/idf/launcher_platform.h` (platform
  wrappers), and other boards' `interface.cpp` for something that already
  does what you need.
- Don't duplicate logic across boards. If two boards need the same
  behavior, it belongs in shared code (`src/hal/**`, `src/**`), not copied
  into each `interface.cpp`.
- Avoid pulling in a new library for one board when an existing one
  already covers the chip family (see `src/hal/README.md`'s per-chip
  tables before assuming a driver doesn't exist yet).
- Guard any library-specific `#include`/code behind the board's selection
  macro (`#ifdef PMIC_BQ25896 ... #endif`), never at file scope — code
  under `src/` compiles for every board, and PlatformIO's dependency
  finder (LDF) uses these guards to decide which libraries to link in.
  Unguarded code pulls the library into boards that don't need it.
- Prefer `-Os`-friendly, straightforward code over template-heavy or
  deeply abstracted C++. Match the style already in the file you're
  editing.
- Don't add speculative flexibility (config options, parameters, feature
  flags) for a use case no board actually has yet.

## Porting to a new board

Follow [`boards/NEW_BOARD_GUIDE.md`](boards/NEW_BOARD_GUIDE.md) — it covers
the env naming convention (`<vendor>-<device>{-<variant>}`), the
`boards/_New-Device-Model/` template, registering the env in
`platformio.ini` and `.github/workflows/main.yml`, and wiring hardware
through the shared HAL. Read [`src/hal/README.md`](src/hal/README.md) for
what the HAL already implements (buttons, encoder, touch, PMIC, gauge,
backlight) before writing board-specific driver code — most new boards
need zero new drivers, only a filled-in `Device*` struct and the right
build flags.

## Project layout

- `boards/<env>/` — one folder per device env: `platformio.ini` (pins,
  chip selection, build flags), `interface.cpp` (device init/input/power
  entry points main.cpp calls into), `connections.md` (pinout/bring-up
  notes for humans).
- `boards/_New-Device-Model/` — the template for new ports; keep it in
  sync with the HAL's actual capabilities (see above).
- `boards/_jsonfiles/` — one generic PlatformIO board JSON per MCU family,
  shared across boards using that chip.
- `src/hal/**` — shared, chip-agnostic drivers used by most boards'
  `interface.cpp`. See `src/hal/README.md`.
- `src/idf/**` — thin ESP-IDF/platform wrappers used in place of
  `<Arduino.h>` calls where the HAL avoids an Arduino dependency.
- `src/**` (root) — the launcher application itself: app registry, backup,
  display, keyboard/input dispatch, OTA, partitioning, etc. — shared
  across every board.
- `lib/**` — vendored/submodule libraries (`DisplayDrivers`, `SensorLib`,
  `XPowersLib`, ...).
- `.github/workflows/main.yml` — CI build matrix; one entry per enabled
  env, must stay in sync with the boards actually meant to build.

## Working with boards

- A board's env name in `platformio.ini`'s `default_envs` and in the CI
  matrix (`.github/workflows/main.yml`) must match its
  `boards/<env>/` folder name and its `[env:<name>]` section exactly.
- New/experimental envs are added commented-out and in alphabetical order
  in both files; uncomment only once the board builds (and ideally has
  been tested on real hardware).
- When editing shared code (`src/**`, `src/hal/**`), consider every board
  that flag/function touches, not just the one you're currently working
  on — a change there is a change for every env in `default_envs`.

## Docs

- [`AGENTS.md`](AGENTS.md) — pointer file for Agents-SDK-based tools, same
  content as this file.
- `docs/` — implementation history of the HAL migration (`plan.md`,
  `etapa_*.md`). Background/history only; `src/hal/README.md` is the
  current, authoritative reference for what the HAL actually contains.

## Build and flashing tests
- Always use `pio run -e <environment>' to test changes
- If building pocess fails due to lfs or LittleFS dependency issues, remove `.pio/core/penv` folder and try again.
- Flahsing tests for command serial verifications must use `esptool` for uploading after build, do not use `pio -e <env> -t upload` command, map the bootloader address according to the ESP32 variant, flash partitions into 0x8000 and firmware into 0x10000 addresses.
