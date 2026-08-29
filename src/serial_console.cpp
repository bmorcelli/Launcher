#include "serial_console.h"

#include "app_registry.h"
#include "idf/idf_update.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "install_shared.h"
#include "launcher_capabilities.h"
#include "nvs_helpers.h"
#include "partition_install_layout.h"
#include "partition_table_model.h"
#include "settings.h"
#include "utils.h"
#include <Arduino.h>
#include <algorithm>
#include <bootloader_common.h>
#include <cstring>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <globals.h>
#include <vector>

// ---------------------------------------------------------------------------
// Serial command grammar (one command per line, space separated tokens):
//
//   nav <NextPress|PrevPress|SelPress|EscPress>   simulate an InputHandler key press
//   reboot                                        free heap objects and restart
//   partitions                                    print the current partition table
//   partition delete <label>                      remove one partition
//   partition deleteall                           remove all OTA app + install data partitions
//   partition edit <label> <offset> <size>        change offset/size (hex "0x.." or decimal)
//   partition create <type> <subtype> <label> <size>
//                                                  append a partition right after the last
//                                                  existing one (offset is automatic);
//                                                  type=app|data, subtype=ota (app) or
//                                                  fat|spiffs|littlefs (data), case insensitive
//   flash firmware <name> <size>                  reserve space, reply "READY <size>", then
//                                                  read <size> raw bytes from Serial and
//                                                  install/boot them as a new app
//   wifi auto                                     connect to the first scanned network with
//                                                  saved credentials, else print the scan
//   wifi scan                                     scan and list nearby networks
//   wifi connect <SSID> [PWD]                     connect (PWD optional if SSID is known)
//   wifi disconnect                                disconnect and stop the WiFi radio
//   wifi add <SSID> <PWD>                          save a network without connecting to it
//   wifi del <SSID>                                remove one saved network
//   wifi clear                                     remove all saved networks
//   wifi list                                      list saved networks, same format as "wifi
//                                                  scan" but rssi=0; auth is "unknown" for
//                                                  networks saved with a password, "open" for
//                                                  networks saved without one
//   settings get                                   print settings[0] as JSON
//   settings set <json>                            replace settings[0] with <json>, apply it to
//                                                  the live globals, and persist it
//   calibrate                                      run the interactive touch calibration wizard
//   calibrate set <Xmax> <Xmin> <Ymax> <Ymin> <rot>
//                                                  write raw calibration values straight to NVS
//   calibrate show                                print the calibration currently saved in NVS
//   calibrate mirror <X/Y>                        flip the given axis and persist it
//   calibrate swapXY                              toggle swapped X/Y and persist it
//   sdio show                                     print effective ESP-Hosted SDIO pins
//                                                  (boards with SDIO2_CLK defined only)
//   sdio set <clk> <cmd> <d0> <d1> <d2> <d3> <rst>
//                                                  override the co-processor SDIO pins in
//                                                  NVS, for boards whose build-time pinout
//                                                  is unconfirmed; takes effect on reboot
//   sdio reset                                     clear the override, back to build-time pins
// This lets a host script recover a device stuck auto-booting a queued OTA
// firmware: send "nav SelPress" while the "Press the button to enter the
// Launcher!" banner is printed (src/main.cpp bootscreen loop) to force the
// Launcher menu instead of the queued app, then drive it entirely over Serial.
// ---------------------------------------------------------------------------

static std::vector<String> splitTokens(const String &line) {
    std::vector<String> tokens;
    String clean;
    clean.reserve(line.length());
    for (size_t i = 0; i < line.length(); ++i) {
        const char c = line[i];
        if (c == '\t') {
            clean += ' ';
        } else if (c >= 0x20 && c != 0x7f) {
            clean += c;
        }
    }
    clean.trim();

    int start = 0;
    int len = static_cast<int>(clean.length());
    while (start < len) {
        while (start < len && clean[start] == ' ') start++;
        int end = start;
        while (end < len && clean[end] != ' ') end++;
        if (end > start) tokens.push_back(clean.substring(start, end));
        start = end;
    }
    return tokens;
}

static uint32_t parseNumber(const String &s) {
    if (s.startsWith("0x") || s.startsWith("0X")) {
        return static_cast<uint32_t>(strtoul(s.c_str() + 2, nullptr, 16));
    }
    return static_cast<uint32_t>(strtoul(s.c_str(), nullptr, 10));
}

static bool looksNumeric(const String &s) {
    if (s.startsWith("0x") || s.startsWith("0X")) return s.length() > 2;
    if (s.length() == 0) return false;
    for (unsigned int i = 0; i < s.length(); i++) {
        if (!isDigit(s[i])) return false;
    }
    return true;
}

// type: "app"/"data" (case insensitive), or a raw decimal/0x-hex value.
static bool resolvePartitionType(const String &s, uint8_t &typeOut) {
    if (s.equalsIgnoreCase("app")) {
        typeOut = 0x00;
        return true;
    }
    if (s.equalsIgnoreCase("data")) {
        typeOut = 0x01;
        return true;
    }
    if (looksNumeric(s)) {
        typeOut = static_cast<uint8_t>(parseNumber(s));
        return true;
    }
    return false;
}

// subtype: "ota" (app, auto-picks the next free OTA slot), "fat"/"spiffs"/"littlefs"
// (data), or a raw decimal/0x-hex value. Case insensitive.
static bool resolvePartitionSubtype(
    uint8_t type, const String &s, const LauncherPartitionTable &table, uint8_t &subtypeOut
) {
    if (type == 0x00 && s.equalsIgnoreCase("ota")) {
        int next = launcherPartitionNextOtaSubtype(table);
        if (next < 0) return false;
        subtypeOut = static_cast<uint8_t>(next);
        return true;
    }
    if (type == 0x01) {
        if (s.equalsIgnoreCase("fat")) {
            subtypeOut = 0x81;
            return true;
        }
        if (s.equalsIgnoreCase("spiffs")) {
            subtypeOut = 0x82;
            return true;
        }
        if (s.equalsIgnoreCase("littlefs")) {
            subtypeOut = 0x83;
            return true;
        }
    }
    if (looksNumeric(s)) {
        subtypeOut = static_cast<uint8_t>(parseNumber(s));
        return true;
    }
    return false;
}

static void printPartitionEditHelp() {
    launcherConsolePrintln("Usage: partition edit <label> <offset> <size>");
    launcherConsolePrintln("  <offset>/<size>: decimal or 0x-prefixed hex");
}

static void printPartitionCreateHelp() {
    launcherConsolePrintln("Usage: partition create <type> <subtype> <label> <size>");
    launcherConsolePrintln("  <type>:    app | data");
    launcherConsolePrintln("  <subtype>: ota                       (type=app)");
    launcherConsolePrintln("             fat | spiffs | littlefs   (type=data)");
    launcherConsolePrintln("  <size>: decimal or 0x-prefixed hex");
}

static void handleNavCommand(const String &target) {
    if (target.equalsIgnoreCase("NextPress") || target.equalsIgnoreCase("next")) {
        NextPress = true;
    } else if (target.equalsIgnoreCase("PrevPress") || target.equalsIgnoreCase("prev")) {
        PrevPress = true;
    } else if (target.equalsIgnoreCase("SelPress") || target.equalsIgnoreCase("sel")) {
        SelPress = true;
    } else if (target.equalsIgnoreCase("EscPress") || target.equalsIgnoreCase("esc")) {
        EscPress = true;
    } else if (target.equalsIgnoreCase("UpPress") || target.equalsIgnoreCase("up")) {
        UpPress = true;
    } else if (target.equalsIgnoreCase("DownPress") || target.equalsIgnoreCase("down")) {
        DownPress = true;
    } else {
        launcherConsolePrintln("ERR unknown nav target");
        return;
    }
    AnyKeyPress = true;
    launcherConsolePrintf("OK nav %s\n", target.c_str());
}

static void handleRebootCommand() {
    launcherConsolePrintln("OK rebooting");
    launcherConsoleFlush();
    launcherDelayMs(100);
    releaseHeapObjectsAndReboot();
}

static const char *partitionTypeName(uint8_t type) {
    if (type == 0x00) return "app";
    if (type == 0x01) return "data";
    return "?";
}

static String partitionSubtypeName(uint8_t type, uint8_t subtype) {
    if (type == 0x00) {
        if (subtype >= 0x10 && subtype <= 0x1F) return "ota_" + String(subtype - 0x10);
    } else if (type == 0x01) {
        switch (subtype) {
            case 0x81: return "fat";
            case 0x82: return "spiffs";
            case 0x83: return "littlefs";
        }
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", subtype);
    return String(buf);
}

// Reads the otadata partition directly to find which OTA app subtype is
// currently selected to boot next (mirrors launcherPartitionSetOtaBoot's format).
static bool getSelectedOtaSubtype(const LauncherPartitionTable &table, uint8_t &subtypeOut) {
    const LauncherPartitionEntry *otadata = launcherPartitionFindOtaData(table);
    if (!otadata || otadata->size < LAUNCHER_FLASH_SECTOR_SIZE * 2) return false;

    esp_ota_select_entry_t entries[2];
    memset(entries, 0xFF, sizeof(entries));
    esp_flash_read(nullptr, &entries[0], otadata->offset, sizeof(entries[0]));
    esp_flash_read(nullptr, &entries[1], otadata->offset + LAUNCHER_FLASH_SECTOR_SIZE, sizeof(entries[1]));

    int active = bootloader_common_get_active_otadata(entries);
    if (active < 0) return false;
    uint32_t seq = entries[active].ota_seq;
    if (seq == 0 || seq == UINT32_MAX) return false;
    subtypeOut = static_cast<uint8_t>(0x10 + ((seq - 1) & 0x0F));
    return true;
}

static void printPartitionTable() {
    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) {
        launcherConsolePrintf("ERR %s\n", error.c_str());
        return;
    }

    uint8_t selectedSubtype = 0xFF;
    bool haveSelected = getSelectedOtaSubtype(table, selectedSubtype);

    launcherConsolePrintln("== Partition table ==");
    for (const auto &entry : table.entries) {
        bool isBootSelected = haveSelected && entry.isOtaApp() && entry.subtype == selectedSubtype;
        launcherConsolePrintf(
            "%-16s type=%-4s subtype=%-9s offset=0x%06X size=0x%06X (%s)%s\n",
            entry.label,
            partitionTypeName(entry.type),
            partitionSubtypeName(entry.type, entry.subtype).c_str(),
            entry.offset,
            entry.size,
            launcherHumanSize(entry.size).c_str(),
            isBootSelected ? "  <-- BOOT" : ""
        );
    }

    uint32_t freeTotal = 0;
    for (const auto &range : launcherPartitionFreeRanges(table)) {
        launcherConsolePrintf(
            "%-16s offset=0x%06X size=0x%06X (%s)\n",
            "<free>",
            range.offset,
            range.size,
            launcherHumanSize(range.size).c_str()
        );
        freeTotal += range.size;
    }
    launcherConsolePrintf(
        "Flash size: %s, free total: %s\n",
        launcherHumanSize(table.flashSize).c_str(),
        launcherHumanSize(freeTotal).c_str()
    );
}

static void handlePartitionCommand(const std::vector<String> &tokens) {
    if (!launcherFirmwareMutationAllowed()) {
        launcherConsolePrintln("ERR firmware/partition mutation disabled on this target");
        return;
    }
    const String &sub = tokens[1];
    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) {
        launcherConsolePrintf("ERR %s\n", error.c_str());
        return;
    }

    if (sub.equalsIgnoreCase("delete") && tokens.size() >= 3) {
        const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, tokens[2].c_str());
        if (!entry) {
            launcherConsolePrintln("ERR partition not found");
            return;
        }
        if (entry->isFactoryOrTestApp()) {
            launcherConsolePrintln("ERR cannot delete the Launcher partition");
            return;
        }
        uint32_t offset = entry->offset;
        if (!launcherPartitionRemoveEntryByOffset(table, offset) ||
            !launcherPartitionWriteGeneratedTable(table, &error)) {
            launcherConsolePrintf("ERR %s\n", error.c_str());
            return;
        }
        launcherConsolePrintln("OK partition deleted");
        return;
    }

    if (sub.equalsIgnoreCase("deleteall")) {
        std::vector<uint32_t> toRemove;
        for (const auto &entry : table.entries) {
            if (entry.isOtaApp()) toRemove.push_back(entry.offset);
        }
        for (uint32_t offset : toRemove) launcherPartitionRemoveEntryByOffset(table, offset);
        launcherPartitionRemoveInstallDataPartitions(table, true);

        if (!launcherPartitionWriteGeneratedTable(table, &error)) {
            launcherConsolePrintf("ERR %s\n", error.c_str());
            return;
        }
        launcherPartitionClearOtaBoot(table, &error);
        launcherClearAppRegistry();
        launcherConsolePrintln("OK all OTA/data partitions deleted");
        return;
    }

    if (sub.equalsIgnoreCase("edit")) {
        if (tokens.size() < 5) {
            printPartitionEditHelp();
            return;
        }
        LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, tokens[2].c_str());
        if (!entry) {
            launcherConsolePrintln("ERR partition not found");
            return;
        }
        entry->offset = parseNumber(tokens[3]);
        entry->size = parseNumber(tokens[4]);
        if (!launcherPartitionValidate(table, &error) ||
            !launcherPartitionWriteGeneratedTable(table, &error)) {
            launcherConsolePrintf("ERR %s\n", error.c_str());
            return;
        }
        launcherConsolePrintln("OK partition edited");
        return;
    }

    if (sub.equalsIgnoreCase("create")) {
        if (tokens.size() < 6) {
            printPartitionCreateHelp();
            return;
        }
        uint8_t type;
        if (!resolvePartitionType(tokens[2], type)) {
            launcherConsolePrintln("ERR unknown partition type");
            printPartitionCreateHelp();
            return;
        }
        uint8_t subtype;
        if (!resolvePartitionSubtype(type, tokens[3], table, subtype)) {
            launcherConsolePrintln("ERR unknown partition subtype");
            printPartitionCreateHelp();
            return;
        }
        const String &label = tokens[4];
        uint32_t size = parseNumber(tokens[5]);

        uint32_t lastEnd = 0;
        for (const auto &entry : table.entries) {
            uint32_t end = entry.offset + entry.size;
            if (end > lastEnd) lastEnd = end;
        }
        uint32_t offset = launcherAlignUp(lastEnd, launcherPartitionAlignment(type, subtype));

        LauncherPartitionEntry newEntry;
        newEntry.type = type;
        newEntry.subtype = subtype;
        newEntry.offset = offset;
        newEntry.size = size;
        strncpy(newEntry.label, label.c_str(), sizeof(newEntry.label) - 1);

        if (!launcherPartitionAdd(table, newEntry, &error) ||
            !launcherPartitionWriteGeneratedTable(table, &error)) {
            launcherConsolePrintf("ERR %s\n", error.c_str());
            return;
        }
        launcherConsolePrintf("OK partition created at offset=0x%06X size=0x%06X\n", offset, size);
        return;
    }

    launcherConsolePrintln("ERR unknown partition subcommand");
}

static void handleFlashCommand(const String &name, uint32_t size) {
    if (!launcherFirmwareMutationAllowed()) {
        launcherConsolePrintln("ERR firmware/partition mutation disabled on this target");
        return;
    }
    if (size == 0) {
        launcherConsolePrintln("ERR invalid size");
        return;
    }

    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) {
        launcherConsolePrintf("ERR %s\n", error.c_str());
        return;
    }

    String label = launcherInstallNextAppLabel(table, name);
    std::vector<LauncherInstallDataPartition> dataPartitions; // app image only, no data partitions
    LauncherPartitionEntry appEntry;
    if (!launcherSelectInstallLayout(table, size, label, dataPartitions, appEntry, error)) {
        launcherConsolePrintf("ERR %s\n", error.c_str());
        return;
    }

    // Tell the host it's safe to start streaming the raw firmware bytes now.
    launcherConsolePrintf("READY %u\n", static_cast<unsigned>(size));

    bool suspendedInput = xHandle != nullptr;
    if (suspendedInput) vTaskSuspend(xHandle);

    unsigned long previousTimeout = Serial.getTimeout();
    Serial.setTimeout(5000);

    // Chunk-and-ACK instead of one long blocking stream read: a blind multi-second
    // stream over a slow (115200 baud) link was observed to silently stall a few
    // percent short of completion on some Windows USB-serial drivers. Acknowledging
    // every chunk means the host never has more than one chunk in flight, which
    // sidesteps that class of driver/buffering issue entirely.
    constexpr size_t kChunkSize = 2048;
    static uint8_t chunkBuffer[kChunkSize];
    bool ok = launcherRawUpdateBegin(appEntry.offset, appEntry.size, size, true);
    size_t written = 0;
    while (ok && written < size) {
        size_t toRead = std::min(kChunkSize, static_cast<size_t>(size) - written);
        size_t got = Serial.readBytes(chunkBuffer, toRead);
        if (got == 0) {
            ok = false;
            error = "Stream read timeout";
            break;
        }
        if (launcherRawUpdateWrite(chunkBuffer, got) != got) {
            ok = false;
            error = launcherUpdateLastErrorName();
            break;
        }
        written += got;
        launcherConsolePrintf("ACK %u/%u\n", static_cast<unsigned>(written), static_cast<unsigned>(size));
    }
    if (ok) ok = launcherRawUpdateEnd();
    if (ok) ok = launcherPartitionWriteGeneratedTable(table, &error);
    if (ok) ok = launcherPartitionSetOtaBoot(table, appEntry.subtype, &error);

    Serial.setTimeout(previousTimeout);

    if (!ok) {
        if (suspendedInput) vTaskResume(xHandle);
        if (error.length()) {
            launcherConsolePrintf("ERR %s\n", error.c_str());
        } else {
            launcherConsolePrintf("ERR flash failed: %s\n", launcherUpdateLastErrorName());
        }
        return;
    }

    launcherSaveInstalledAppMetadata(table, appEntry, name, name, {});
    saveIntoNVS();

    // saveIntoNVS() (and other calls above it) may print without a trailing
    // newline; force a fresh line so a host parsing "line startswith OK/ERR"
    // never sees this result glued onto the end of an unrelated log line.
    launcherConsolePrintln("");
    launcherConsolePrintln("OK flashed, rebooting");
    launcherConsoleFlush();
    launcherDelayMs(200);
    releaseHeapObjectsAndReboot();
}

static const char *wifiAuthModeName(wifi_auth_mode_t mode) {
    switch (mode) {
        case WIFI_AUTH_OPEN: return "open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK: return "WAPI";
        case WIFI_AUTH_OWE: return "OWE";
        default: return "?";
    }
}

static void printWifiScanResults(const std::vector<LauncherWifiAp> &networks) {
    launcherConsolePrintln("== WiFi networks ==");
    for (const auto &ap : networks) {
        if (ap.ssid.empty()) continue;
        String knownPwd;
        bool saved = getWifiCredential(ap.ssid.c_str(), knownPwd);
        launcherConsolePrintf(
            "%-32s rssi=%-4d auth=%-9s%s\n",
            ap.ssid.c_str(),
            ap.rssi,
            wifiAuthModeName(ap.authmode),
            saved ? "  (Saved)" : ""
        );
    }
}

static void handleWifiScanCommand() {
    std::vector<LauncherWifiAp> networks;
    int nets = launcherWifiScan(networks);
    if (nets < 0) {
        launcherConsolePrintln("ERR scan failed");
        return;
    }
    printWifiScanResults(networks);
}

// Polls a just-started connection attempt, mirroring the retry/timeout policy of
// the interactive wifiConnect() in onlineLauncher.cpp but without any UI.
constexpr int kSerialWifiConnectAttempts = 20; // ~10 s at 500 ms/poll

static bool attemptWifiConnect(const String &ssid, const String &password, String &errorOut) {
    int count = 0;
    while (true) {
        LauncherWifiConnectState state = launcherWifiConnectStatus(ssid.c_str(), password.c_str(), 500);
        if (state == LauncherWifiConnectState::Connected) return true;
        if (state == LauncherWifiConnectState::WrongPassword) {
            errorOut = "wrong password";
            return false;
        }
        if (state == LauncherWifiConnectState::Failed || count > kSerialWifiConnectAttempts) {
            errorOut = "connect failed";
            return false;
        }
        count++;
    }
}

static void handleWifiAutoCommand() {
    std::vector<LauncherWifiAp> networks;
    int nets = launcherWifiScan(networks);
    if (nets < 0) {
        launcherConsolePrintln("ERR scan failed");
        return;
    }

    for (const auto &ap : networks) {
        if (ap.ssid.empty()) continue;
        String targetSsid = ap.ssid.c_str();
        String knownPwd;
        if (!getWifiCredential(targetSsid, knownPwd)) continue;

        String error;
        if (attemptWifiConnect(targetSsid, knownPwd, error)) {
            launcherConsolePrintf(
                "OK connected to %s, ip=%s\n", targetSsid.c_str(), launcherWifiLocalIp().c_str()
            );
            return;
        }
        launcherConsolePrintf("WARN %s: %s\n", targetSsid.c_str(), error.c_str());
    }

    launcherConsolePrintln("ERR no known network found");
    printWifiScanResults(networks);
}

static void handleWifiConnectCommand(const std::vector<String> &tokens) {
    if (tokens.size() < 3) {
        launcherConsolePrintln("Usage: wifi connect <SSID> [PWD]");
        return;
    }
    const String &targetSsid = tokens[2];
    bool havePassword = tokens.size() >= 4;

    String knownPwd;
    bool known = getWifiCredential(targetSsid, knownPwd);
    String password = havePassword ? tokens[3] : knownPwd;
    if (!havePassword && !known) {
        launcherConsolePrintln("ERR unknown SSID, password required");
        return;
    }

    String error;
    if (!attemptWifiConnect(targetSsid, password, error)) {
        launcherConsolePrintf("ERR %s\n", error.c_str());
        return;
    }

    if (havePassword && (!known || knownPwd != password)) {
        setWifiCredential(targetSsid, password, true);
        saveConfigs();
    }

    launcherConsolePrintf("OK connected to %s, ip=%s\n", targetSsid.c_str(), launcherWifiLocalIp().c_str());
}

static void handleWifiDisconnectCommand() {
    if (!launcherWifiStop()) {
        launcherConsolePrintln("ERR disconnect failed");
        return;
    }
    launcherConsolePrintln("OK disconnected");
}

static void handleWifiAddCommand(const std::vector<String> &tokens) {
    if (tokens.size() < 4) {
        launcherConsolePrintln("Usage: wifi add <SSID> <PWD>");
        return;
    }
    const String &targetSsid = tokens[2];
    const String &password = tokens[3];
    if (!setWifiCredential(targetSsid, password, true)) {
        launcherConsolePrintln("ERR failed to save network");
        return;
    }
    launcherConsolePrintf("OK saved %s\n", targetSsid.c_str());
}

static void handleWifiDelCommand(const std::vector<String> &tokens) {
    if (tokens.size() < 3) {
        launcherConsolePrintln("Usage: wifi del <SSID>");
        return;
    }
    const String &targetSsid = tokens[2];
    if (!removeWifiCredential(targetSsid)) {
        launcherConsolePrintln("ERR SSID not found");
        return;
    }
    launcherConsolePrintf("OK removed %s\n", targetSsid.c_str());
}

static void handleWifiClearCommand() {
    if (!clearWifiCredentials()) {
        launcherConsolePrintln("ERR failed to clear networks");
        return;
    }
    launcherConsolePrintln("OK cleared all networks");
}

static void handleWifiListCommand() {
    launcherConsolePrintln("== WiFi networks ==");
    for (const auto &net : getSavedWifiNetworks()) {
        launcherConsolePrintf(
            "%-32s rssi=%-4d auth=%-9s\n", net.ssid.c_str(), 0, net.hasPassword ? "unknown" : "open"
        );
    }
}

static void handleWifiCommand(const std::vector<String> &tokens) {
    const String &sub = tokens[1];
    if (sub.equalsIgnoreCase("auto")) {
        handleWifiAutoCommand();
    } else if (sub.equalsIgnoreCase("scan")) {
        handleWifiScanCommand();
    } else if (sub.equalsIgnoreCase("connect")) {
        handleWifiConnectCommand(tokens);
    } else if (sub.equalsIgnoreCase("disconnect")) {
        handleWifiDisconnectCommand();
    } else if (sub.equalsIgnoreCase("add")) {
        handleWifiAddCommand(tokens);
    } else if (sub.equalsIgnoreCase("del")) {
        handleWifiDelCommand(tokens);
    } else if (sub.equalsIgnoreCase("clear")) {
        handleWifiClearCommand();
    } else if (sub.equalsIgnoreCase("list")) {
        handleWifiListCommand();
    } else if (sub.equalsIgnoreCase("hosted")) {
        // Clears the latched "co-processor is broken" verdict. Needed after
        // flashing esp_hosted firmware onto the co-processor, otherwise the
        // guard keeps skipping bring-up forever.
        launcherWifiHostedResetGuard();
        launcherConsolePrintln("OK hosted guard cleared, reboot to probe again");
    } else {
        launcherConsolePrintln("ERR unknown wifi subcommand");
    }
}

#if defined(HAS_RESISTIVE_TOUCH)
static void printTouchCalibration(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint8_t rot) {
    // rot packs (see settings.cpp calibrateTouch()): bit0 = !swapXY, bit1 = invertY,
    // bit2 = !invertX — the bits are inverted from their plain meaning for legacy
    // compatibility, so decode them back to plain booleans for display.
    const bool swapXY = !(rot & 0x01);
    const bool mirrorX = !(rot & 0x04);
    const bool mirrorY = (rot & 0x02) != 0;
    launcherConsolePrintf(
        "x0:%u x1:%u y0:%u y1:%u rot:0x%02X (swapXY:%u mirrorX:%u mirrorY:%u)\n",
        x0,
        x1,
        y0,
        y1,
        rot,
        swapXY,
        mirrorX,
        mirrorY
    );
}

static void handleCalibrateShowCommand() {
    uint16_t x0, x1, y0, y1;
    uint8_t rot;
    if (!getTouchCalibration(x0, x1, y0, y1, rot)) {
        launcherConsolePrintln("ERR no calibration saved");
        return;
    }
    printTouchCalibration(x0, x1, y0, y1, rot);
}

static void handleCalibrateSetCommand(const std::vector<String> &tokens) {
    if (tokens.size() < 7) {
        launcherConsolePrintln("ERR usage: calibrate set <Xmax> <Xmin> <Ymax> <Ymin> <rot>");
        return;
    }
    const uint16_t xMax = static_cast<uint16_t>(parseNumber(tokens[2]));
    const uint16_t xMin = static_cast<uint16_t>(parseNumber(tokens[3]));
    const uint16_t yMax = static_cast<uint16_t>(parseNumber(tokens[4]));
    const uint16_t yMin = static_cast<uint16_t>(parseNumber(tokens[5]));
    const uint8_t rot = static_cast<uint8_t>(parseNumber(tokens[6]));
    if (!saveTouchCalibration(xMin, xMax, yMin, yMax, rot)) {
        launcherConsolePrintln("ERR invalid calibration values");
        return;
    }
    loadTouchCalibration();
    launcherConsolePrintln("OK calibration saved");
}

static void handleCalibrateMirrorCommand(const std::vector<String> &tokens) {
    if (tokens.size() < 3) {
        launcherConsolePrintln("ERR usage: calibrate mirror <X/Y>");
        return;
    }
    uint16_t x0, x1, y0, y1;
    uint8_t rot;
    if (!getTouchCalibration(x0, x1, y0, y1, rot)) {
        launcherConsolePrintln("ERR no calibration saved");
        return;
    }
    if (tokens[2].equalsIgnoreCase("X")) {
        rot ^= 0x04;
    } else if (tokens[2].equalsIgnoreCase("Y")) {
        rot ^= 0x02;
    } else {
        launcherConsolePrintln("ERR axis must be X or Y");
        return;
    }
    if (!saveTouchCalibration(x0, x1, y0, y1, rot)) {
        launcherConsolePrintln("ERR failed to save calibration");
        return;
    }
    loadTouchCalibration();
    printTouchCalibration(x0, x1, y0, y1, rot);
}

static void handleCalibrateSwapCommand() {
    uint16_t x0, x1, y0, y1;
    uint8_t rot;
    if (!getTouchCalibration(x0, x1, y0, y1, rot)) {
        launcherConsolePrintln("ERR no calibration saved");
        return;
    }
    rot ^= 0x01;
    if (!saveTouchCalibration(x0, x1, y0, y1, rot)) {
        launcherConsolePrintln("ERR failed to save calibration");
        return;
    }
    loadTouchCalibration();
    printTouchCalibration(x0, x1, y0, y1, rot);
}

static void handleCalibrateCommand(const std::vector<String> &tokens) {
    if (tokens.size() < 2) {
        launcherConsolePrintln("Starting calibration..");
        calibrateTouch();
        return;
    }
    const String &sub = tokens[1];
    if (sub.equalsIgnoreCase("set")) {
        handleCalibrateSetCommand(tokens);
    } else if (sub.equalsIgnoreCase("show")) {
        handleCalibrateShowCommand();
    } else if (sub.equalsIgnoreCase("mirror")) {
        handleCalibrateMirrorCommand(tokens);
    } else if (sub.equalsIgnoreCase("swapXY")) {
        handleCalibrateSwapCommand();
    } else {
        launcherConsolePrintln("ERR unknown calibrate subcommand");
    }
}
#endif

#if defined(SDIO2_CLK)
static void printSdioPin(const char *key) {
    lnvs::Handle h(SDIO_OVERRIDE_NVS_NS, false);
    int value;
    if (h && lnvs::getInt(h.raw(), key, value)) launcherConsolePrintf("  %s: %d (override)\n", key, value);
    else launcherConsolePrintf("  %s: (build default)\n", key);
}

static void handleSdioShowCommand() {
    for (const char *key : {"clk", "cmd", "d0", "d1", "d2", "d3", "rst"}) printSdioPin(key);
}

static void handleSdioSetCommand(const std::vector<String> &tokens) {
    if (tokens.size() < 9) {
        launcherConsolePrintln("ERR usage: sdio set <clk> <cmd> <d0> <d1> <d2> <d3> <rst>");
        return;
    }
    lnvs::Handle h(SDIO_OVERRIDE_NVS_NS, true);
    if (!h) {
        launcherConsolePrintln("ERR could not open NVS");
        return;
    }
    static const char *keys[7] = {"clk", "cmd", "d0", "d1", "d2", "d3", "rst"};
    for (int i = 0; i < 7; i++) lnvs::setInt(h.raw(), keys[i], (int)parseNumber(tokens[2 + i]));
    h.commit();
    launcherConsolePrintln("SDIO override saved. Reboot to apply.");
}

static void handleSdioResetCommand() {
    lnvs::eraseNamespace(SDIO_OVERRIDE_NVS_NS);
    launcherConsolePrintln("SDIO override cleared. Reboot to apply.");
}

static void handleSdioCommand(const std::vector<String> &tokens) {
    if (tokens.size() < 2) {
        launcherConsolePrintln("ERR usage: sdio <show|set|reset>");
        return;
    }
    if (tokens[1].equalsIgnoreCase("show")) handleSdioShowCommand();
    else if (tokens[1].equalsIgnoreCase("set")) handleSdioSetCommand(tokens);
    else if (tokens[1].equalsIgnoreCase("reset")) handleSdioResetCommand();
    else launcherConsolePrintln("ERR usage: sdio <show|set|reset>");
}
#endif

static void handleSettingsPrintCommand() { printSettingsJson(); }

static void handleSettingsLoadCommand(const String &json) {
    if (json.length() == 0) {
        launcherConsolePrintln("Usage: settings set <json>");
        return;
    }
    if (!loadSettingsJson(json)) {
        launcherConsolePrintln("ERR invalid settings json");
        return;
    }
    launcherConsolePrintln("OK settings loaded");
}

static bool stripCommandPrefix(const String &line, const char *prefix, String &rest) {
    size_t prefixLen = strlen(prefix);
    if (line.length() < prefixLen) return false;
    if (!line.substring(0, prefixLen).equalsIgnoreCase(prefix)) return false;
    rest = line.substring(prefixLen);
    rest.trim();
    return true;
}

static void printVersion() { launcherConsolePrintln("Launcher " LAUNCHER); }

static void printWhoami() { launcherConsolePrintln(device_name.c_str()); }

static void printHelp() {
    launcherConsolePrintln("Commands:");
    launcherConsolePrintln("  nav <NextPress|PrevPress|SelPress|EscPress>");
#if defined(HAS_RESISTIVE_TOUCH)
    launcherConsolePrintln("  calibrate");
    launcherConsolePrintln("  calibrate set <Xmax> <Xmin> <Ymax> <Ymin> <rot>");
    launcherConsolePrintln("  calibrate show");
    launcherConsolePrintln("  calibrate mirror <X/Y>");
    launcherConsolePrintln("  calibrate swapXY");
#endif
    launcherConsolePrintln("  reboot");
    launcherConsolePrintln("  partitions");
    launcherConsolePrintln("  partition delete <label>");
    launcherConsolePrintln("  partition deleteall");
    launcherConsolePrintln("  partition edit <label> <offset> <size>");
    launcherConsolePrintln("  partition create <type> <subtype> <label> <size>");
    launcherConsolePrintln("  flash firmware <name> <size>");
    launcherConsolePrintln("  wifi auto");
    launcherConsolePrintln("  wifi scan");
    launcherConsolePrintln("  wifi connect <SSID> [PWD]");
    launcherConsolePrintln("  wifi disconnect");
    launcherConsolePrintln("  wifi add <SSID> <PWD>");
    launcherConsolePrintln("  wifi del <SSID>");
    launcherConsolePrintln("  wifi clear");
    launcherConsolePrintln("  wifi list");
    launcherConsolePrintln("  wifi hosted retry");
    launcherConsolePrintln("  settings get");
    launcherConsolePrintln("  settings set <json>");
#if defined(SDIO2_CLK)
    launcherConsolePrintln("  sdio show");
    launcherConsolePrintln("  sdio set <clk> <cmd> <d0> <d1> <d2> <d3> <rst>");
    launcherConsolePrintln("  sdio reset");
#endif
}

static void handleSerialCommand(const String &line) {
    String settingsJson;
    if (stripCommandPrefix(line, "settings set ", settingsJson)) {
        handleSettingsLoadCommand(settingsJson);
        return;
    }

    std::vector<String> tokens = splitTokens(line);
    if (tokens.empty()) return;
    const String &cmd = tokens[0];

    if (cmd.equalsIgnoreCase("nav") && tokens.size() >= 2) {
        handleNavCommand(tokens[1]);
    } else if (cmd.equalsIgnoreCase("reboot")) {
        handleRebootCommand();
    } else if (cmd.equalsIgnoreCase("partitions")) {
        printPartitionTable();
    } else if (cmd.equalsIgnoreCase("partition") && tokens.size() >= 2) {
        handlePartitionCommand(tokens);
    } else if (
        cmd.equalsIgnoreCase("flash") && tokens.size() >= 4 && tokens[1].equalsIgnoreCase("firmware")
    ) {
        handleFlashCommand(tokens[2], parseNumber(tokens[3]));
    } else if (cmd.equalsIgnoreCase("wifi") && tokens.size() >= 2) {
        handleWifiCommand(tokens);
    } else if (cmd.equalsIgnoreCase("settings") && tokens.size() >= 2 && tokens[1].equalsIgnoreCase("get")) {
        handleSettingsPrintCommand();
    } else if (cmd.equalsIgnoreCase("settings")) {
        launcherConsolePrintln("Usage: settings <get|set <json>>");
    } else if (cmd.equalsIgnoreCase("help")) {
        printHelp();
#if defined(HAS_RESISTIVE_TOUCH)
    } else if (cmd.equalsIgnoreCase("calibrate")) {
        handleCalibrateCommand(tokens);
#endif
#if defined(SDIO2_CLK)
    } else if (cmd.equalsIgnoreCase("sdio") && tokens.size() >= 2) {
        handleSdioCommand(tokens);
#endif
    } else if (cmd.equalsIgnoreCase("version")) {
        printVersion();
    } else if (cmd.equalsIgnoreCase("whoami")) {
        printWhoami();
    } else {
        launcherConsolePrintln("ERR unknown command, type 'help' for command list");
    }
}

void taskSerialConsole(void *parameter) {
    String buffer;
    while (true) {
        while (Serial.available() > 0) {
            char c = static_cast<char>(Serial.read());
            if (c == '\r') continue;
            if (c == '\n') {
                String line = buffer;
                buffer = "";
                line.trim();
                if (line.length() > 0) handleSerialCommand(line);
                // Stop draining here: a just-handled "flash firmware" command may have
                // already consumed the binary payload directly from Serial; re-checking
                // Serial.available() from the top keeps that byte stream untouched by
                // this line-oriented reader.
                break;
            }
            buffer += c;
            if (buffer.length() > 8192) buffer = "";
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
