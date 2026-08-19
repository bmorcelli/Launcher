// SPDX-FileCopyrightText: 2026 bmorcelli
//
// SPDX-License-Identifier: MIT
//
// Stand-ins for functions display.cpp's translation unit references from
// other modules (app_registry.cpp, mykeyboard.cpp, onlineLauncher.cpp,
// powerSave.cpp, settings.cpp) that this harness doesn't compile. None of
// these are reachable from the menu-drawing path this harness actually
// drives — they only exist to satisfy the linker for lambdas that display.cpp
// defines but nothing here calls. Extend as needed when wiring up more
// screens: add the declaration's header include, then a matching stub here.

#include <DisplayDrivers.h>

#include <app_registry.h>
#include <display.h>
#include <globals.h>
#include <mykeyboard.h>
#include <onlineLauncher.h>
#include <partitioner.h>
#include <powerSave.h>
#include <sd_functions.h>
#include <serial_console.h>
#include <settings.h>
#include <utils.h>
#include <webInterface.h>

#include <ArduinoJson/Memory/Allocator.hpp>

#include <cstdarg>
#include <cstdio>
#include <mutex>

bool launcherBootAppByLabel(const char *) { return false; }
String launcherSelectedBootAppName() { return String(); }
std::vector<LauncherAppMetadata> launcherListInstalledApps() { return {}; }
bool launcherBootInstalledAppOrShowMenu() { return false; }
bool launcherBootCurrentApp() { return false; }
void launcherShowAppActions(const char *) {}

bool launcherWifiIsConnected() { return false; }

String keyboard(String mytext, int, const String &) { return mytext; }

ArduinoJson::Allocator *launcherJsonAllocator() {
    return ArduinoJson::detail::DefaultAllocator::instance();
}

void installFirmwareFromManifest(const String &, const String &, String) {}
void downloadFirmware(const String &, String, String, String, const String &, bool) {}
bool checkForUpdates() { return false; }
bool GetJsonFromLauncherHub(uint8_t, const String &, bool, const String &) { return false; }

// ---------------------------------------------------------------------------
// Fake screens — CFG/SD/OTA/WUI wired to the real drawing/navigation code
// (loopOptions, loopFirmware, loopVersions, from display.cpp) with made-up
// data standing in for settings.cpp/sd_functions.cpp/onlineLauncher.cpp/
// webInterface.cpp, none of which this harness compiles.
// ---------------------------------------------------------------------------

// CFG: a settings-style options list, real loopOptions().
void settings_menu() {
    options = {
        {"Brightness",       [] {}                            },
        {"Rotation",         [] {}                            },
        {"UI Colors",        [] {}                            },
        {"Wifi Credentials", [] {}                            },
        {"Main Menu",        [] { returnToMenu = true; }},
    };
    loopOptions(options);
}

// SD: 3 fake folders + 3 fake files, real loopOptions(). Folders get
// FGCOLOR-0x1111 — the same colour loopSD/loopOptions itself uses to tell a
// folder row from a file row.
String loopSD(bool) {
    const uint16_t folderColor = static_cast<uint16_t>(FGCOLOR - 0x1111);
    options = {
        {"Documents/",   [] {}, folderColor},
        {"Firmware/",    [] {}, folderColor},
        {"Backup/",      [] {}, folderColor},
        {"readme.txt",   [] {}            },
        {"config.conf",  [] {}            },
        {"firmware.bin", [] {}            },
    };
    loopOptions(options);
    return "";
}

// OTA: a fake firmware list in `doc`, then the real loopFirmware()/
// loopVersions() (display.cpp) draw and navigate it exactly like the real
// online list would.
JsonDocument getVersionInfo(const String &fid) {
    JsonDocument item;
    item["name"] = "Fake Firmware";
    item["author"] = "native";
    item["fid"] = fid;
    item["star"] = false;
    JsonArray versions = item["versions"].to<JsonArray>();
    JsonObject v1 = versions.add<JsonObject>();
    v1["version"] = "1.2.0";
    v1["published_at"] = "2026-01-01";
    v1["file"] = "firmware.bin";
    JsonObject v2 = versions.add<JsonObject>();
    v2["version"] = "1.1.0";
    v2["published_at"] = "2025-06-01";
    v2["file"] = "firmware_old.bin";
    return item;
}

void ota_function() {
    doc.clear();
    doc["page"] = 1;
    doc["page_size"] = 10;
    JsonArray items = doc["items"].to<JsonArray>();
    struct FakeFw {
        const char *name, *author, *fid;
        bool star;
    };
    const FakeFw fake[] = {
        {"Marauder", "justcallmekoko", "fw-marauder", true},
        {"Bruce", "pr3y", "fw-bruce", false},
        {"SomeFirmware", "someone", "fw-x", false},
    };
    for (const auto &f : fake) {
        JsonObject it = items.add<JsonObject>();
        it["name"] = f.name;
        it["author"] = f.author;
        it["fid"] = f.fid;
        it["star"] = f.star;
        it["file"] = "firmware.bin";
        it["version"] = "1.0.0";
    }
    total_firmware = 3;
    current_page = 1;
    loopFirmware(false);
}

// WUI: the screen shown after the web server actually starts (real
// startWebUiLoopCommon(), webInterface.cpp) — fake IP, real credentials.
void loopOptionsWebUi() {
    tft->fillScreen(BGCOLOR);
    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, ALCOLOR);
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
    tft->setTextColor(ALCOLOR, BGCOLOR);
    tft->setTextSize(_fp);
    tft->drawCentreString("-= Launcher WebUI =-", tftWidth / 2, 10, 1);
    tft->drawCentreString("http://launcher.local", tftWidth / 2, 22, 1);
    tft->setTextColor(FGCOLOR, BGCOLOR);
    tft->setTextSize(_fm);
    tftprintln("IP 192.168.4.1", 10, 1);
    tftprintln("Usr: " + String(wui_usr), 10, 1);
    tftprintln("Pwd: " + String(wui_pwd), 10, 1);
    tft->setTextColor(ALCOLOR, BGCOLOR);
    tft->setTextSize(_fp);
    tft->drawCentreString("press Esc to stop", tftWidth / 2, tftHeight - 15, 1);
    tft->display(false);
    while (!check(EscPress)) vTaskDelay(pdMS_TO_TICKS(10));
    tft->fillScreen(BGCOLOR);
}

bool wakeUpScreen() { return false; }
void setBrightness(int, bool) {}
void saveConfigs() {}
bool getFromNVS() { return false; }
bool getWifiFromNVS() { return false; }
void getConfigs() {}
void getBrightness() {}
bool ensureM5StackUiFlowNVSDefaults() { return true; }

void partitionCrawler() {}
void launcherPartitionInitDefaultSizes() {}
void partList() {}

bool releaseHeapObjectsAndReboot() { return true; }

void checkPowerSaveTime() {}

// A real mutex, not a no-op: taskInputHandler (main.cpp) runs on its own
// thread via the xTaskCreate shim and writes PrevPress/NextPress/KeyStroke/...
// while loop() reads them on the main thread — same producer/consumer shape
// as on real hardware (input task vs. loopTask), so it needs the same kind of
// guard around a torn read/write.
static std::mutex g_inputMutex;
void launcherInputLockInit() {}
void launcherInputLock() { g_inputMutex.lock(); }
void launcherInputUnlock() { g_inputMutex.unlock(); }

// Serial-monitor command console: nothing types into this harness's stdin in
// a way that matters yet, so the task just parks itself instead of spinning.
void taskSerialConsole(void *) {
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

void launcherConsolePrintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
void launcherConsolePrint(const char *text) {
    if (text) fputs(text, stdout);
}
void launcherConsolePrintln(const char *text) {
    launcherConsolePrint(text);
    fputc('\n', stdout);
}
void launcherConsoleBegin(unsigned long) {}
void launcherConsoleFlush() { fflush(stdout); }
void launcherConsoleEnd() {}

// mykeyboard.h/settings.h declare these __attribute__((weak)) with no strong
// definition anywhere in this harness — an undefined weak symbol links clean
// but resolves to address 0, so calling one segfaults instead of erroring at
// link time.
void powerOff() {}
void reboot() {}
void checkReboot() {}
int getBattery() { return 0; }
void _setBrightness(uint8_t) {}

// sd_functions.h declares this extern; sd_functions.cpp (the real definition)
// isn't compiled here.
SPIClass sdcardSPI;
