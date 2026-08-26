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

// Must precede <DisplayDrivers.h>: native_sdl.h drags in ArduinoJson.h at its
// bottom, and settings.cpp (included below) hands ArduinoJson a bare String
// and a Stream/Print-derived File to (de)serialize config.conf through — the
// generic Reader/Writer ArduinoJson falls back to otherwise assumes a raw
// stream cursor, which neither of those is. These macros are normally
// auto-enabled by ARDUINO being defined; native_sdl.h deliberately doesn't
// pull in Arduino.h (see its header comment), so they're set by hand here,
// scoped to this translation unit only — the other DisplayDrivers backend
// .cpp files (gxepd2_hal, lovyan, ardgfx, ...) that the library also builds
// stay untouched, since each is a separate TU.
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 1
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 1
#include <Print.h>
#include <Stream.h>

#include <DisplayDrivers.h>

#include <app_registry.h>
#include <backup_manager.h>
#include <display.h>
#include <globals.h>
#include <idf/idf_update.h>
#include <idf/idf_wifi.h>
#include <install_shared.h>
#include <littlefs_patch.h>
#include <mykeyboard.h>
#include <nvs_helpers.h>
#include <onlineLauncher.h>
#include <partition_install_layout.h>
#include <partition_table_model.h>
#include <partitioner.h>
#include <powerSave.h>
#include <sd_functions.h>
#include <serial_console.h>
#include <settings.h>
#include <utils.h>
#include <webInterface.h>
#include <wifi_crypto.h>

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

ArduinoJson::Allocator *launcherJsonAllocator() { return ArduinoJson::detail::DefaultAllocator::instance(); }

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

// CFG: the real settings.cpp (settings_menu, setBrightnessMenu, gsetRotation,
// setUiColor, NVS load/save, ...) compiled below against the fake nvs.h/
// esp_mac.h in native/sources/ — same tier as SD's sd_functions.cpp below.
// NVS reads always miss and writes always "succeed" without persisting, so
// every settings screen behaves as if it's running with defaults every time.
#include "../../src/settings.cpp"

// settings.cpp reaches into lnvs:: (nvs_helpers.cpp), wifiPwdEncrypt/Decrypt
// (wifi_crypto.cpp) and the hosted-wifi co-processor guard (idf_wifi.cpp) —
// none of those translation units are compiled here. Reads always miss and
// writes always report success without persisting or encrypting anything,
// matching the "settings screens run with defaults every time" behavior
// above.
bool hostedWifiAvailable = false;
void launcherWifiHostedResetGuard() {}

String wifiPwdEncrypt(const String &plain) { return plain; }
String wifiPwdDecrypt(const String &cipher) { return cipher; }

namespace lnvs {
Handle::Handle(const char *, bool) {}
Handle::~Handle() {}
bool Handle::open(const char *, bool) { return false; }
bool Handle::commit() { return false; }

std::vector<String> keys(const char *, nvs_type_t) { return {}; }
bool exists(const char *) { return false; }
bool eraseNamespace(const char *) { return true; }
String getString(nvs_handle_t, const char *, size_t) { return String(); }
String getString(const char *, const char *, size_t) { return String(); }
bool setString(nvs_handle_t, const char *, const char *) { return true; }
bool setString(const char *, const char *, const char *) { return true; }
bool eraseKey(nvs_handle_t, const char *) { return true; }
bool getBool(nvs_handle_t, const char *, bool &) { return false; }
bool setBool(nvs_handle_t, const char *, bool) { return true; }
bool getInt(nvs_handle_t, const char *, int &) { return false; }
bool setInt(nvs_handle_t, const char *, int) { return true; }
bool copyBlob(nvs_handle_t, const char *, nvs_handle_t, const char *, size_t) { return false; }
const char *typeName(nvs_type_t) { return ""; }
nvs_type_t typeFromName(const char *) { return NVS_TYPE_ANY; }
bool getScalar(nvs_handle_t, const char *, nvs_type_t, int64_t &) { return false; }
bool setScalar(nvs_handle_t, const char *, nvs_type_t, int64_t) { return true; }
} // namespace lnvs

// SD: real sd_functions.cpp (loopSD/readFs, real folder-tree navigation,
// "> Back", sorting, ...) compiled below against the fake filesystem in
// native/sources/FS.h — see NativeUI's README for the include-path trick.
// The install/backup machinery it also compiles in (flashing a selected
// .bin, partition table edits) isn't exercised by browsing and is stubbed
// out below, just enough to link.
#include "../../src/sd_functions.cpp"

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
        {"Marauder",     "justcallmekoko", "fw-marauder", true },
        {"Bruce",        "pr3y",           "fw-bruce",    false},
        {"SomeFirmware", "someone",        "fw-x",        false},
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
    tft->drawCentreString("http://launcher.local\n", tftWidth / 2, 22, 1);
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

void partitionCrawler() {}
void launcherPartitionInitDefaultSizes() {}
// PMan: a fake partition table, real loopOptions(). partitioner.cpp isn't
// compiled here (2000+ lines of real flash/MD5 partition-table editing) —
// this is the same tier as CFG/SD/OTA/WUI, not the "real code, fake data
// underneath" tier SD gets. Selecting an entry doesn't do anything (yet);
// extend the lambdas here to fake a partition's action screen the same way
// loopSD/ota_function do, if that's useful.
void partList() {
    options = {
        {"factory  0x010000  2.5MB",  [] {}                            },
        {"nvs      0x009000  20KB",   [] {}                            },
        {"otadata  0x00E000  8KB",    [] {}                            },
        {"ota_0    0x290000  2.5MB",  [] {}                            },
        {"ota_1    0x520000  2.5MB",  [] {}                            },
        {"spiffs   0x7B0000  2.5MB",  [] {}                            },
        {"coredump 0xA40000  64KB",   [] {}                            },
        {"Main Menu",                 [] { returnToMenu = true; }},
    };
    loopOptions(options);
}

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
void launcherConsolePrintLong(const char *text) { launcherConsolePrint(text); }
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

// ---------------------------------------------------------------------------
// Low-level stubs for sd_functions.cpp's install/backup/partition-table code
// paths — reachable only by actually flashing a selected file, not by
// browsing, so these only need to link, not work.
// ---------------------------------------------------------------------------
bool launcherSelectHeld() { return false; }

size_t launcherUpdateWrite(const uint8_t *, size_t) { return 0; }
void launcherUpdateAbort() {}
bool launcherUpdateIsFinished() { return true; }
int launcherUpdateLastError() { return 0; }
const char *launcherUpdateLastErrorName() { return "n/a"; }
bool launcherRawUpdateBegin(uint32_t, size_t, size_t, bool) { return false; }
size_t launcherRawUpdateWrite(const uint8_t *, size_t) { return 0; }
bool launcherRawUpdateEnd() { return false; }
bool launcherRawErase(uint32_t, size_t) { return false; }
bool launcherRawPrepareDataPartition(uint32_t, size_t) { return false; }
bool launcherClearCoredump() { return false; }
bool launcherUpdateErasePartition(const esp_partition_t *) { return false; }
bool launcherUpdateCopyPartition(const esp_partition_t *, const esp_partition_t *, LauncherUpdateProgress) {
    return false;
}
bool launcherUpdateRepairPartitionTable(uint32_t, bool *) { return false; }
bool launcherRawUpdateStream(Stream &, uint32_t, size_t, size_t, bool, LauncherUpdateProgress) {
    return false;
}

bool launcherPatchReducedLittlefsSuperblocks(uint32_t, uint32_t, String *, bool *) { return false; }

String launcherInstallAppDisplayName(const String &sourceName, const String &) { return sourceName; }
void launcherSaveInstalledAppMetadata(
    const LauncherPartitionTable &, const LauncherPartitionEntry &, const String &, const String &,
    const std::vector<String> &, const String &
) {}

bool launcherPartitionReadCurrent(LauncherPartitionTable &, String *) { return false; }
bool launcherPartitionValidate(const LauncherPartitionTable &, String *) { return false; }
bool launcherPartitionWriteGeneratedTable(const LauncherPartitionTable &, String *) { return false; }
String launcherPartitionNextAppLabel(const LauncherPartitionTable &, const String &installedName) {
    return installedName;
}
uint32_t launcherAlignUp(uint32_t value, uint32_t alignment) {
    return alignment ? ((value + alignment - 1) / alignment) * alignment : value;
}
uint32_t launcherPartitionBoundedPayloadSize(uint32_t declaredSize, uint32_t, uint32_t, uint32_t) {
    return declaredSize;
}
LauncherPartitionPayloadPlan
launcherPartitionFatPayloadPlan(const char *, uint32_t declaredSize, uint32_t, uint32_t) {
    return LauncherPartitionPayloadPlan{declaredSize, declaredSize};
}
bool launcherPartitionSetOtaBoot(const LauncherPartitionTable &, uint8_t, String *) { return false; }
String generateAppNum(const String &) { return "00000000"; }
BackupInstallInfo loadInstalledFromConfig(const String &appNum) {
    BackupInstallInfo info;
    info.appNum = appNum;
    return info;
}
bool restorePartitionFromBackupDirect(const char *, const char *, uint32_t, uint32_t) { return false; }
bool restorePartitionFromBackup(const char *, const char *) { return false; }
bool saveInstalledToConfig(const BackupInstallInfo &) { return false; }

bool launcherSelectInstallLayout(
    LauncherPartitionTable &, size_t, const String &, std::vector<LauncherInstallDataPartition> &,
    LauncherPartitionEntry &, String &error
) {
    error = "install not supported in native harness";
    return false;
}
