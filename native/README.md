# Launcher UI on the desktop

Compiles the **real** `src/main.cpp` and `src/display.cpp` — real `setup()`,
real `loop()`, the real menu items and navigation, real `settings_menu()` /
`loopFirmware()` / `loopVersions()` / `loopOptions()` drawing code — against
an SDL2 window instead of a panel, using DisplayDrivers' `USE_NATIVE_SDL`
backend (`lib/DisplayDrivers/examples/NativeUI` has the backend's own
README). Good for iterating on menu layout, colors, or a new screen without
flashing a board every time.

Verified end-to-end on WSL2 (Ubuntu): built, launched a window, sat through
the real boot-wait screen into the real main menu, navigated with real
arrow-key input into CFG/SD/OTA/WUI, and confirmed each renders through the
real drawing functions with fake data.

## Prerequisites

Same as `lib/DisplayDrivers/examples/NativeUI`:

```
sudo apt install -y build-essential libsdl2-dev   # Linux / WSL, verified
```

Windows/macOS: see that example's README — not verified by me, WSL is the
tested path.

## Running it

```
cd native
pio run -e native -t upload
```

Arrow keys are a 5-button pad, Enter is Select, Esc is Escape/back. The real
`setup()` runs its real ~5s boot-wait screen first (press Enter to skip it
immediately) before showing the real main menu.

## What each menu item does here

The real menu items are what real `main.cpp` builds (SD/OTA/WUI/PMan/CFG/OFF
+ any installed apps, minus the ones this harness can't provide — see
below). Their *actions* are real too, except four are given fake data in
place of what would normally come from SD/WiFi:

| Item | Real function called | What's faked |
|------|----------------------|---------------|
| SD   | `loopSD()`            | 3 fake folders + 3 fake files, not a real card |
| OTA  | `ota_function()` → `loopFirmware()` / `loopVersions()` | 3 fake firmware entries in `doc`, fake version info |
| WUI  | `loopOptionsWebUi()`  | The "server started" screen with a fake IP, real configured user/pass |
| CFG  | `settings_menu()`     | A fake settings list (Brightness/Rotation/Colors/Wifi Credentials) |

All four are implemented in `src/stubs.cpp` — real Launcher code
(`settings.cpp`, `sd_functions.cpp`, `onlineLauncher.cpp`, `webInterface.cpp`)
isn't compiled here, so these stand in for it, populating the same globals
(`options`, `doc`, `total_firmware`) the real ones would and then calling the
**real** `loopOptions()` / `loopFirmware()` / `loopVersions()` from
`display.cpp` to draw and navigate them.

## How this works

`src/main.cpp` and `src/display.cpp` are each a single translation unit that
`#include`s a lot of project headers (`settings.h`, `sd_functions.h`,
`onlineLauncher.h`, `powerSave.h`, `mykeyboard.h`, `app_registry.h`,
`idf/idf_wifi.h`, ...) and, transitively through `globals.h`, several
Arduino/ESP-IDF-only ones (`SD.h`, `FreeRTOS.h`, `LittleFS.h`, `Arduino.h`,
...). None of the real Launcher source under `src/` or `include/` is
modified — instead:

- **`sources/`** holds fake versions of every ESP32/Arduino-only header those
  translation units need just to *compile* (`SD.h`, `FS.h`, `FFat.h`,
  `SPIFFS.h`, `Arduino.h`, `Stream.h`, `freertos/*`, `driver/gpio.h`,
  `esp_partition.h`, `esp_ota_ops.h`, `esp_wifi_types.h`, `esp_random.h`,
  `esp_timer.h`, `nvs_flash.h`, `pins_arduino.h`, `WString.h`).
  `platformio.ini` puts `-Isources` **before** `-I../src -I../include`, so
  `#include <SD.h>` resolves to the fake while `#include "settings.h"` still
  resolves to the real Launcher header.
- **`src/main.cpp`** `#include`s `../../src/display.cpp` and
  `../../src/main.cpp` directly (a single-extra-file unity-build trick —
  simpler than wiring them up as PlatformIO library sources), defines the
  handful of extern globals the fake headers declare (`Serial`, `SD`, `FFat`,
  `SPIFFS`), and implements `InputHandler()` — the one piece a board's
  `interface.cpp` would normally provide — by polling the SDL window's
  keyboard state instead of real GPIO. `-DHAS_5_BUTTONS=1`
  (`platformio.ini`) is what makes real `loop()` compile in the branch that
  reads `UpPress`/`DownPress` for grid navigation.
- **`freertos/task.h`**'s `xTaskCreate` shim spawns a real `std::thread` —
  real `main.cpp` uses it to start `taskInputHandler` (which calls
  `InputHandler()` in a loop, same as on a board) and `taskSerialConsole`, so
  both genuinely run concurrently with the main thread's `loop()`, the same
  producer/consumer shape as the real firmware's input task vs. loopTask —
  which is also why `launcherInputLock()`/`Unlock()` are a real `std::mutex`
  here, not no-ops.
- **`src/stubs.cpp`** provides definitions for every function `main.cpp`'s
  and `display.cpp`'s *other* code paths call into that this harness doesn't
  compile the real implementation of — `wakeUpScreen()`, `nvs_flash_init()`,
  `partitionCrawler()`, etc. — plus the four fake screens documented above.

## Extending this

**A menu item's action needs new data or a new fake screen**: edit its
implementation in `src/stubs.cpp`.

**A function the linker names that isn't stubbed yet**: add a stub in
`src/stubs.cpp` — include the real header it's declared in, copy the
signature, return a sensible default (`false`/empty/no-op) — until it links.
Watch for functions declared `__attribute__((weak))` with no strong
definition anywhere (see `mykeyboard.h`, `settings.h`) — those link *clean*
but resolve to address 0, so calling one segfaults instead of failing at link
time. Stub these explicitly too, don't rely on the linker to catch them.

## What this does not cover

No real WiFi, SD, BLE, NVS, or OTA — those are stubbed out (see above), not
implemented. `launcherListInstalledApps()` always returns empty, so no
installed-app shortcuts appear. This draws and navigates the real screens
with fake data; test real firmware behaviour (actual card contents, actual
network calls, actual reboots) on a board.
