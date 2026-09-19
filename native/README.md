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
arrow-key input into CFG/SD/OTA/WUI/PMan, and confirmed each renders through
the real drawing functions with fake data — SD goes a level deeper still (see
table below): it's the real `sd_functions.cpp` walking a fake in-memory
folder tree, folder-into-folder and "> Back" both confirmed working.

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
below). Two different depths of "real" are in play:

| Item | What's compiled and running | What's faked |
|------|------------------------------|---------------|
| SD   | **Real `sd_functions.cpp`**: `loopSD()`, `readFs()`, `sortList()`, folder traversal, "> Back" | The filesystem underneath (`native/sources/FS.h`'s `kFakeFs` in-memory tree) — not a real card |
| OTA  | Real `loopFirmware()` / `loopVersions()` (`display.cpp`) | `ota_function()` (`stubs.cpp`) seeds `doc` with 3 fake firmware entries instead of calling the real online API |
| WUI  | Real `tft`/`tftprintln` calls | `loopOptionsWebUi()` (`stubs.cpp`) draws the real "server started" layout with a fake IP instead of running a real web server |
| CFG  | Real `loopOptions()` (`display.cpp`) | `settings_menu()` (`stubs.cpp`) hands it a fake settings list instead of the real one |
| PMan | Real `loopOptions()` (`display.cpp`) | `partList()` (`stubs.cpp`) hands it a fake partition table instead of reading real flash — `partitioner.cpp` (2000+ lines of real flash/MD5 partition-table editing) isn't compiled here, unlike SD |

SD is the deepest: the actual navigation logic runs unmodified, fed by a fake
filesystem. The other four stop one level higher — a stub builds fake data
and hands it to the real drawing/navigation function, but the module that
would normally produce that data (`onlineLauncher.cpp`, `webInterface.cpp`,
`settings.cpp`, `partitioner.cpp`) isn't compiled. Going as deep on any of
them as SD is a matter of compiling that real `.cpp` too and building a fake
version of whatever it reads (a fake `doc`-shaped JSON, a fake flash-backed
partition table, ...) — see "Extending this" below.

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

**A menu item's fake data needs tweaking**: edit its implementation in
`src/stubs.cpp` (`settings_menu()`, `ota_function()`, `loopOptionsWebUi()`,
`partList()`) or, for SD, `native/sources/FS.h`'s `kFakeFs` table.

**Going one level deeper on OTA/WUI/CFG/PMan, the way SD already is** — i.e.
compiling the real `onlineLauncher.cpp` / `webInterface.cpp` / `settings.cpp`
/ `partitioner.cpp` instead of stubbing the whole module:

1. `#include` the real `.cpp` from `src/stubs.cpp` (see how SD does it —
   `#include "../../src/sd_functions.cpp"`) instead of writing your own
   `ota_function()`/etc.
2. Build. The linker will name whatever ESP32-only functions that file calls
   into that aren't stubbed yet.
3. For each: either add a fake header to `sources/` (if it's a *type* the
   real code needs — that's what `FS.h`'s fake filesystem is, or what a fake
   `esp_partition_t` table would be for `partitioner.cpp`), or a stub
   function in `src/stubs.cpp` (if it's a *behaviour* nothing here needs to
   actually perform — `false`/empty/no-op is usually enough) — until it
   links.
4. Watch for functions declared `__attribute__((weak))` with no strong
   definition anywhere (see `mykeyboard.h`, `settings.h`) — those link
   *clean* but resolve to address 0, so calling one segfaults instead of
   failing at link time. Stub these explicitly too, don't rely on the linker
   to catch them.

This is real work per module (SD took a fake filesystem plus ~25 low-level
stubs for the install/backup code paths it also compiles in, even though
they're unreachable from plain browsing) — worth it for a module whose
*navigation logic itself* is what you're testing, not just its screen.

## What this does not cover

No real WiFi, SD, BLE, NVS, or OTA — those are stubbed out (see above), not
implemented. `launcherListInstalledApps()` always returns empty, so no
installed-app shortcuts appear. This draws and navigates the real screens
with fake data; test real firmware behaviour (actual card contents, actual
network calls, actual reboots) on a board.
