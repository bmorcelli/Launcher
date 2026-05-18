#include "onlineLauncher.h"
#include "app_registry.h"
#include "display.h"
#include "idf/idf_http_client.h"
#include "idf/idf_update.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "partition_table_model.h"
#include "powerSave.h"
#include "sd_functions.h"
#include "settings.h"
#include <esp_ota_ops.h>
#include <globals.h>

#define M5_SERVER_PATH "https://m5burner-cdn.m5stack.com/firmware/"

/***************************************************************************************
** Function name: wifiConnect
** Description:   Connects to wifiNetwork
***************************************************************************************/
void wifiConnect(String ssid, int encryptation, bool isAP) {
    if (!isAP) {
        bool found = false;
        bool wrongPass = false;
        getConfigs();

        String knownPwd;
        if (getWifiCredential(ssid, knownPwd)) {
            pwd = knownPwd;
            found = true;
            launcherConsolePrintf("Found SSID: %s\n", ssid.c_str());
        }
        launcherConsolePrintf("sdcardMounted: %d\n", sdcardMounted);

    Retry:
        if (!found || wrongPass) {
            if (encryptation > 0) {
                pwd = keyboard(pwd, 63, "Network Password:");
                if (pwd == String(KEY_ESCAPE)) {
                    returnToMenu = true;
                    goto END;
                }
            }

            if (!found) {
                if (setWifiCredential(ssid, pwd)) {
                    found = true;
                    launcherConsolePrintf("wifiConnect: ssid->%s, pwd->%s\n", ssid.c_str(), pwd.c_str());
                    saveConfigs();
                } else {
                    launcherConsolePrintln("wifiConnect: failed to store new WiFi entry");
                }
            } else if (wrongPass) {
                if (setWifiCredential(ssid, pwd)) {
                    launcherConsolePrintf("Mudou pwd de SSID: %s\n", ssid.c_str());
                    saveConfigs();
                }
            }
        }

        resetTftDisplay(10, 10, FGCOLOR, FP);
        tft->fillScreen(BGCOLOR);
        tftprint("Connecting to: " + ssid + ".", 10);
        tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);

        int count = 0;
        bool connected = false;
        while (!connected) {
            connected = launcherWifiConnect(ssid.c_str(), pwd.c_str(), 500);
            if (connected) break;
            vTaskDelay(500 / portTICK_PERIOD_MS);
            tftprint(".", 10);
            count++;
            if (count > 10) {
                wrongPass = true;
                options = {
                    {"Retry",     [&]() { yield(); }            },
                    {"Main Menu", [&]() { returnToMenu = true; }},
                };
                loopOptions(options);
                if (!returnToMenu) goto Retry;
                else goto END;
            }
            tft->display(false);
        }
    } else { // Running in Access point mode
#if !CONFIG_ESP_HOSTED_ENABLED
        launcherWifiStop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
#endif
        launcherWifiStartAp("Launcher", "", 6, 4);
        vTaskDelay(250 / portTICK_PERIOD_MS);
        launcherConsolePrintf("IP: %s\n", launcherWifiApIp().c_str());
    }
END:
    launcherDelayMs(0);
}
void connectWifi() {
    displayRedStripe("Scanning...");
#if CONFIG_ESP_HOSTED_ENABLED
    launcherWifiStop();
#endif
    std::vector<LauncherWifiAp> networks;
    int nets = launcherWifiScan(networks);
    // Serial.printf("connectWifi: scan returned %d networks\n", nets);
    options = {};
    for (int i = 0; i < nets; i++) {
        String networkSsid = networks[i].ssid.c_str();
        if (networkSsid.isEmpty()) continue;
        int authMode = static_cast<int>(networks[i].authmode);
        options.push_back({networkSsid, [=]() { wifiConnect(networkSsid, authMode); }});
    }
    options.push_back({"Hidden SSID", [=]() {
                           String __ssid = keyboard("", 32, "Your SSID");
                           if (__ssid != String(KEY_ESCAPE)) wifiConnect(__ssid.c_str(), 8);
                       }});
    options.push_back({"Main Menu", [=]() { returnToMenu = true; }});
    loopOptions(options);
}
/***************************************************************************************
** Function name: ota_function
** Description:   Start OTA function
***************************************************************************************/
void ota_function() {
#ifndef DISABLE_OTA
    bool fav = false;
    if (!launcherWifiIsConnected()) connectWifi();
    if (launcherWifiIsConnected()) {
        // Debug
        // Serial.printf("Favorite size: %d\n", favorite.size());
        // serializeJsonPretty(favorite, Serial);
        // Debug
        if (favorite.size() > 0) {
            options = {
                {"OTA List",      [&]() { fav = false; }        },
                {"Favorite List", [&]() { fav = true; }         },
                {"Main Menu",     [=]() { returnToMenu = true; }}
            };
            loopOptions(options);
        }
        if (returnToMenu) return;
        if (fav) {
            int idx = 0;
            auto NavMenu = [&](int fw) {
                options.clear();
                if (favorite[fw]["fid"].as<String>().length() > 0) {
                    options.push_back({"View firmware", [=]() {
                                           loopVersions(favorite[fw]["fid"].as<String>());
                                       }});
                } else {
                    options.push_back({"Install", [=]() {
                                           installExtFirmware(favorite[fw]["link"].as<String>());
                                       }});
                }
                options.push_back({"Remove Favorite", [=]() {
                                       favorite.remove(fw);
                                       saveConfigs();
                                   }});
                options.push_back({"Back to List", [=]() { /* Do nothing, just return */ }});
                options.push_back({"Main Menu", [=]() { returnToMenu = true; }});
                loopOptions(options);
            };
        RELOAD:
            options.clear();
            int count = 0;
            for (JsonObject item : favorite) {
                options.push_back({item["name"].as<String>(), [=]() { NavMenu(count); }});
                count++;
            }
            options.push_back({"Main Menu", [=]() { returnToMenu = true; }, ALCOLOR});
            idx = loopOptions(options, false, FGCOLOR, BGCOLOR, false, idx);
            if (!returnToMenu && idx != -1) goto RELOAD;
        } else {
            if (GetJsonFromLauncherHub()) loopFirmware();
        }
    }
    tft->fillScreen(BGCOLOR);
#endif
}

#ifndef DISABLE_OTA

/***************************************************************************************
** Function name: replaceChars
** Description:   Replace some characters for _
***************************************************************************************/
String replaceChars(String input) {
    // Define os caracteres que devem ser substituídos
    const char charsToReplace[] = {'<', '>', ':', '\"', '/', '\\', '|', '?', '*', '\'', '`', '&'};
    // Define o caractere de substituição (neste exemplo, usamos um espaço)
    const char replacementChar = '_';

    // Percorre a string e substitui os caracteres especificados
    for (size_t i = 0; i < sizeof(charsToReplace); i++) {
        input.replace(String(charsToReplace[i]), String(replacementChar));
    }

    for (size_t i = 0; i < input.length(); i++) {
        if (input[i] < 32) { input.setCharAt(i, replacementChar); }
    }

    input.trim();
    while (input.endsWith(".") || input.endsWith(" ")) { input.remove(input.length() - 1); }

    if (input.isEmpty()) input = "firmware";

    return input;
}

struct RangeBufferContext {
    uint8_t *buffer;
    size_t capacity;
    size_t written;
};

bool rangeBufferCb(const uint8_t *data, size_t len, void *ctx) {
    RangeBufferContext *range = static_cast<RangeBufferContext *>(ctx);
    if (!range || !range->buffer || range->written + len > range->capacity) return false;
    memcpy(range->buffer + range->written, data, len);
    range->written += len;
    return true;
}

static uint8_t inputHandlerPauseDepth = 0;

void pauseInputHandlerTask() {
    if (!xHandle) return;
    if (inputHandlerPauseDepth++ == 0) vTaskSuspend(xHandle);
}

void resumeInputHandlerTask() {
    if (!xHandle || inputHandlerPauseDepth == 0) return;
    inputHandlerPauseDepth--;
    if (inputHandlerPauseDepth == 0) vTaskResume(xHandle);
}

bool discardHttpCb(const uint8_t *, size_t, void *) { return true; }

bool parseContentRangeTotal(const char *contentRange, size_t &total) {
    if (!contentRange) return false;
    String range = contentRange;
    int slash = range.lastIndexOf('/');
    if (slash < 0 || slash + 1 >= range.length()) return false;
    total = range.substring(slash + 1).toInt();
    return total > 0;
}

bool getRemoteFileSize(const String &url, size_t &size, const char *hwid = nullptr) {
    LauncherHttpResponse response;
    if (!launcherHttpGetRange(url.c_str(), 0, 1, discardHttpCb, nullptr, &response, hwid)) return false;
    if (response.status != 206) return false;
    return parseContentRangeTotal(response.content_range, size);
}

struct FileDownloadContext {
    File *file;
    size_t downloaded;
    size_t expected;
    long progressTick;
    LauncherHttpResponse *response; // back-pointer to get content_length once headers arrive
};

bool fileDownloadCb(const uint8_t *data, size_t len, void *ctx) {
    FileDownloadContext *download = static_cast<FileDownloadContext *>(ctx);
    if (!download || !download->file) return false;

    // On the first chunk, content_length is already populated by fetch_headers.
    // Use it to initialize the progress bar with the real file size.
    if (download->expected == 0 && download->response && download->response->content_length > 0) {
        download->expected = static_cast<size_t>(download->response->content_length);
        progressHandler(0, download->expected);
    }

    size_t wrote = download->file->write(data, len);
    if (wrote != len) return false;
    download->downloaded += wrote;

    if (download->expected > 0) {
        if (download->progressTick >= 10) {
            tft->drawPixel(0, 0, 0);
            progressHandler(download->downloaded, download->expected);
            download->progressTick = 0;
        } else {
            download->progressTick++;
        }
    }
    return true;
}

struct HttpUpdateContext {
    LauncherUpdateTarget target;
    size_t expected;
    size_t written;
    bool started;
};

struct RawHttpUpdateContext {
    uint32_t address;
    size_t partitionSize;
    size_t expected;
    size_t written;
    bool appImage;
    bool started;
};

bool launcherUpdateHttpCb(const uint8_t *data, size_t len, void *ctx) {
    HttpUpdateContext *updateCtx = static_cast<HttpUpdateContext *>(ctx);
    if (!updateCtx) return false;
    if (!updateCtx->started) {
        if (!launcherUpdateBegin(updateCtx->target, updateCtx->expected)) return false;
        updateCtx->started = true;
        progressHandler(0, updateCtx->expected);
    }
    const size_t remaining =
        updateCtx->written < updateCtx->expected ? updateCtx->expected - updateCtx->written : 0;
    const size_t writeLen = len > remaining ? remaining : len;
    if (writeLen == 0) return true;
    size_t wrote = launcherUpdateWrite(data, writeLen);
    if (wrote != writeLen) {
        launcherConsolePrintf(
            "HTTP update write failed target=%d chunk=%u write=%u wrote=%u remaining=%u err=%s\n",
            updateCtx->target,
            static_cast<unsigned>(len),
            static_cast<unsigned>(writeLen),
            static_cast<unsigned>(wrote),
            static_cast<unsigned>(remaining),
            launcherUpdateLastErrorName()
        );
        return false;
    }
    updateCtx->written += wrote;
    progressHandler(updateCtx->written, updateCtx->expected);
    return true;
}

bool launcherRawUpdateHttpCb(const uint8_t *data, size_t len, void *ctx) {
    RawHttpUpdateContext *updateCtx = static_cast<RawHttpUpdateContext *>(ctx);
    if (!updateCtx) return false;
    if (!updateCtx->started) {
        if (!launcherRawUpdateBegin(
                updateCtx->address, updateCtx->partitionSize, updateCtx->expected, updateCtx->appImage
            )) {
            return false;
        }
        updateCtx->started = true;
        progressHandler(0, updateCtx->expected);
    }
    const size_t remaining =
        updateCtx->written < updateCtx->expected ? updateCtx->expected - updateCtx->written : 0;
    const size_t writeLen = len > remaining ? remaining : len;
    if (writeLen == 0) return true;
    size_t wrote = launcherRawUpdateWrite(data, writeLen);
    if (wrote != writeLen) {
        launcherConsolePrintf(
            "HTTP raw write failed app=%d chunk=%u write=%u wrote=%u written=%u expected=%u remaining=%u "
            "err=%s\n",
            updateCtx->appImage,
            static_cast<unsigned>(len),
            static_cast<unsigned>(writeLen),
            static_cast<unsigned>(wrote),
            static_cast<unsigned>(updateCtx->written),
            static_cast<unsigned>(updateCtx->expected),
            static_cast<unsigned>(remaining),
            launcherUpdateLastErrorName()
        );
        return false;
    }
    updateCtx->written += wrote;
    progressHandler(updateCtx->written, updateCtx->expected);
    return true;
}

String nextOnlineAppLabel(const LauncherPartitionTable &table) {
    int highest = 0;
    for (const LauncherPartitionEntry &entry : table.entries) {
        if (strncmp(entry.label, "app", 3) != 0) continue;
        const char *cursor = entry.label + 3;
        if (*cursor == '\0') continue;
        bool numeric = true;
        int value = 0;
        while (*cursor) {
            if (*cursor < '0' || *cursor > '9') {
                numeric = false;
                break;
            }
            value = value * 10 + (*cursor - '0');
            cursor++;
        }
        if (numeric && value > highest) highest = value;
    }
    return "app" + String(highest + 1);
}

bool findOrCreateDataPartition(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, uint32_t requestedSize,
    LauncherPartitionEntry &entry, String &error
) {
    LauncherPartitionEntry *existing = launcherPartitionFindByLabel(table, label);
    if (existing) {
        if (existing->isData() && existing->subtype == subtype && existing->size >= requestedSize) {
            entry = *existing;
            return true;
        }
        error = String("Partition ") + label + " is too small or incompatible";
        return false;
    }
    return launcherPartitionCreateData(table, subtype, label, requestedSize, &entry, &error);
}

uint32_t alignOnlineUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t onlineDefaultSpiffsSize() { return LAUNCHER_DEFAULT_SPIFFS_SIZE; }

uint32_t onlineLargeSpiffsThreshold() { return LAUNCHER_DEFAULT_SPIFFS_THRESHOLD; }

uint32_t onlineUseRemainingSpiffsSize() { return 0xFFFFFFFF; }

bool createDataInLargestFreeRange(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, LauncherPartitionEntry &entry,
    String &error
) {
    LauncherPartitionRange best;
    for (const LauncherPartitionRange &range : launcherPartitionFreeRanges(table)) {
        const uint32_t alignedOffset = alignOnlineUp(range.offset, LAUNCHER_FLASH_SECTOR_SIZE);
        if (alignedOffset < range.offset || range.size < alignedOffset - range.offset) continue;
        const uint32_t alignedSize =
            (range.size - (alignedOffset - range.offset)) & ~(LAUNCHER_FLASH_SECTOR_SIZE - 1);
        if (alignedSize > best.size) best = {alignedOffset, alignedSize};
    }
    if (best.size == 0) {
        error = "No free partition range large enough";
        return false;
    }

    LauncherPartitionEntry created;
    created.type = 0x01;
    created.subtype = subtype;
    created.offset = best.offset;
    created.size = best.size;
    created.flags = 0;
    memset(created.label, 0, sizeof(created.label));
    strncpy(created.label, label, 15);
    if (!launcherPartitionAdd(table, created, &error)) return false;
    entry = created;
    return true;
}

String onlineHexSize(uint32_t value) {
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "0x%06lX", static_cast<unsigned long>(value));
    return String(buffer);
}

String onlineHumanSize(uint32_t value) {
    if (value >= 1024 * 1024 && value % (1024 * 1024) == 0) { return String(value / (1024 * 1024)) + "MB"; }
    if (value >= 1024 && value % 1024 == 0) { return String(value / 1024) + "KB"; }
    return String(value) + " bytes";
}

String onlineSizeLabel(uint32_t value) { return onlineHexSize(value) + " (" + onlineHumanSize(value) + ")"; }

bool removeEntryByOffset(LauncherPartitionTable &table, uint32_t offset) {
    for (auto it = table.entries.begin(); it != table.entries.end(); ++it) {
        if (it->offset == offset) {
            table.entries.erase(it);
            return true;
        }
    }
    return false;
}

bool isReplaceableOnlineApp(const LauncherPartitionEntry &entry) {
    if (!entry.isOtaApp()) return false;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->address == entry.offset) return false;
    return true;
}

bool isRemovableOnlineInstallData(const LauncherPartitionEntry &entry) {
    if (!entry.isData()) return false;
    if (entry.subtype != 0x81 && entry.subtype != 0x82 && entry.subtype != 0x83) return false;
    return strcmp(entry.label, "spiffs") == 0 || strcmp(entry.label, "sys") == 0 ||
           strcmp(entry.label, "vfs") == 0;
}

bool removeOnlineInstallDataPartitions(LauncherPartitionTable &table, bool removeSpiffs) {
    bool removed = false;
    for (auto it = table.entries.begin(); it != table.entries.end();) {
        bool removable = isRemovableOnlineInstallData(*it);
        if (removable && !removeSpiffs && strcmp(it->label, "spiffs") == 0) removable = false;
        if (removable) {
            it = table.entries.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    return removed;
}

bool addManualAppEntry(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, uint32_t offset, uint32_t size,
    LauncherPartitionEntry &created, String &error
) {
    LauncherPartitionEntry entry;
    entry.type = 0x00;
    entry.subtype = subtype;
    entry.offset = offset;
    entry.size = alignOnlineUp(size, LAUNCHER_APP_PARTITION_ALIGNMENT);
    entry.flags = 0;
    memset(entry.label, 0, sizeof(entry.label));
    strncpy(entry.label, label, 15);
    if (!launcherPartitionAdd(table, entry, &error)) return false;
    created = entry;
    return true;
}

bool prepareDynamicDataPartitions(
    LauncherPartitionTable &table, bool spiffs, uint32_t spiffsSize, LauncherPartitionEntry &spiffsEntry,
    bool &hasSpiffsEntry, bool fat, uint32_t fatSize[2], String fatLabel[2],
    LauncherPartitionEntry fatEntry[2], bool hasFatEntry[2], String &error
) {
    hasSpiffsEntry = false;
    hasFatEntry[0] = false;
    hasFatEntry[1] = false;

    auto prepareSpiffs = [&]() {
        LauncherPartitionEntry *existing = launcherPartitionFindByLabel(table, "spiffs");
        if (existing) {
            if (!existing->isData() || existing->subtype != 0x82) {
                error = "Partition spiffs is incompatible";
                return false;
            }
            if (spiffsSize != onlineUseRemainingSpiffsSize() && existing->size < spiffsSize) {
                error = "Partition spiffs is too small or incompatible";
                return false;
            }
            spiffsEntry = *existing;
        } else if (spiffsSize == onlineUseRemainingSpiffsSize()) {
            if (!createDataInLargestFreeRange(table, 0x82, "spiffs", spiffsEntry, error)) return false;
        } else {
            if (!findOrCreateDataPartition(table, 0x82, "spiffs", spiffsSize, spiffsEntry, error))
                return false;
        }
        hasSpiffsEntry = true;
        return true;
    };

    if (spiffs && spiffsSize > 0 && spiffsSize != onlineUseRemainingSpiffsSize()) {
        if (!prepareSpiffs()) return false;
    }

    if (fat) {
        for (int i = 0; i < 2; ++i) {
            if (fatSize[i] == 0) continue;
            const char *label = fatLabel[i].isEmpty() ? (i == 0 ? "sys" : "vfs") : fatLabel[i].c_str();
            uint32_t desiredSize = launcherPartitionDefaultFatSize(label);
            if (desiredSize < fatSize[i]) desiredSize = fatSize[i];
            if (!findOrCreateDataPartition(table, 0x81, label, desiredSize, fatEntry[i], error)) return false;
            hasFatEntry[i] = true;
        }
    }

    if (spiffs && spiffsSize == onlineUseRemainingSpiffsSize()) {
        if (!prepareSpiffs()) return false;
    }
    return true;
}

bool selectInstallLayout(
    LauncherPartitionTable &table, size_t updateSize, const String &defaultLabel, bool spiffs,
    uint32_t spiffsSize, bool fat, uint32_t fatSize[2], String fatLabel[2], LauncherPartitionEntry &appEntry,
    LauncherPartitionEntry &spiffsEntry, bool &hasSpiffsEntry, LauncherPartitionEntry fatEntry[2],
    bool hasFatEntry[2], String &error
) {
    const uint32_t requiredAppPartitionSize =
        alignOnlineUp(static_cast<uint32_t>(updateSize), LAUNCHER_APP_PARTITION_ALIGNMENT);
    uint32_t requiredInstallSize = requiredAppPartitionSize;
    if (fat) {
        for (int i = 0; i < 2; ++i) requiredInstallSize += fatSize[i];
    }
    if (spiffs && spiffsSize > 0 && spiffsSize != onlineUseRemainingSpiffsSize()) {
        requiredInstallSize += spiffsSize;
    }
    std::vector<LauncherPartitionEntry> originalApps;
    for (const LauncherPartitionEntry &entry : table.entries) {
        if (isReplaceableOnlineApp(entry)) originalApps.push_back(entry);
    }

    LauncherPartitionTable directCandidate = table;
    LauncherPartitionEntry directApp;
    LauncherPartitionEntry directSpiffs;
    LauncherPartitionEntry directFat[2];
    bool directHasSpiffs = false;
    bool directHasFat[2] = {false, false};
    if (launcherPartitionCreateOtaApp(
            directCandidate, updateSize, defaultLabel.c_str(), &directApp, &error
        ) &&
        prepareDynamicDataPartitions(
            directCandidate,
            spiffs,
            spiffsSize,
            directSpiffs,
            directHasSpiffs,
            fat,
            fatSize,
            fatLabel,
            directFat,
            directHasFat,
            error
        ) &&
        launcherPartitionValidate(directCandidate, &error)) {
        table = directCandidate;
        appEntry = directApp;
        spiffsEntry = directSpiffs;
        hasSpiffsEntry = directHasSpiffs;
        for (int i = 0; i < 2; ++i) {
            fatEntry[i] = directFat[i];
            hasFatEntry[i] = directHasFat[i];
        }
        return true;
    }

    directCandidate = table;
    directApp = LauncherPartitionEntry();
    directSpiffs = LauncherPartitionEntry();
    directFat[0] = LauncherPartitionEntry();
    directFat[1] = LauncherPartitionEntry();
    directHasSpiffs = false;
    directHasFat[0] = false;
    directHasFat[1] = false;
    if (prepareDynamicDataPartitions(
            directCandidate,
            spiffs,
            spiffsSize,
            directSpiffs,
            directHasSpiffs,
            fat,
            fatSize,
            fatLabel,
            directFat,
            directHasFat,
            error
        ) &&
        launcherPartitionCreateOtaApp(
            directCandidate, updateSize, defaultLabel.c_str(), &directApp, &error
        ) &&
        launcherPartitionValidate(directCandidate, &error)) {
        table = directCandidate;
        appEntry = directApp;
        spiffsEntry = directSpiffs;
        hasSpiffsEntry = directHasSpiffs;
        for (int i = 0; i < 2; ++i) {
            fatEntry[i] = directFat[i];
            hasFatEntry[i] = directHasFat[i];
        }
        return true;
    }

    if (originalApps.empty() &&
        std::any_of(table.entries.begin(), table.entries.end(), isRemovableOnlineInstallData)) {
        for (int removalPass = 0; removalPass < 2; ++removalPass) {
            const bool removeSpiffs = removalPass == 1;
            directCandidate = table;
            directApp = LauncherPartitionEntry();
            directSpiffs = LauncherPartitionEntry();
            directFat[0] = LauncherPartitionEntry();
            directFat[1] = LauncherPartitionEntry();
            directHasSpiffs = false;
            directHasFat[0] = false;
            directHasFat[1] = false;

            if (!removeOnlineInstallDataPartitions(directCandidate, removeSpiffs)) continue;
            if (launcherPartitionCreateOtaApp(
                    directCandidate, updateSize, defaultLabel.c_str(), &directApp, &error
                ) &&
                prepareDynamicDataPartitions(
                    directCandidate,
                    spiffs,
                    spiffsSize,
                    directSpiffs,
                    directHasSpiffs,
                    fat,
                    fatSize,
                    fatLabel,
                    directFat,
                    directHasFat,
                    error
                ) &&
                launcherPartitionValidate(directCandidate, &error)) {
                table = directCandidate;
                appEntry = directApp;
                spiffsEntry = directSpiffs;
                hasSpiffsEntry = directHasSpiffs;
                for (int i = 0; i < 2; ++i) {
                    fatEntry[i] = directFat[i];
                    hasFatEntry[i] = directHasFat[i];
                }
                launcherConsolePrintf(
                    "OTA recreated %s data partition(s) for initial install\n", removeSpiffs ? "all" : "FAT"
                );
                return true;
            }
        }
    }

    launcherConsolePrintf("OTA direct layout failed: %s\n", error.c_str());
    for (const LauncherPartitionRange &range : launcherPartitionFreeRanges(table)) {
        launcherConsolePrintf("OTA free range: offset=0x%06X size=0x%06X\n", range.offset, range.size);
    }

    LauncherPartitionTable original = table;
    std::vector<Option> choices;
    std::vector<String> choiceLabels;
    choices.push_back({String("Need ") + onlineSizeLabel(requiredInstallSize), []() {}});
    choiceLabels.push_back(choices.back().label);

    auto addChoice = [&](const String &label,
                         const LauncherPartitionTable &candidate,
                         const LauncherPartitionEntry &candidateApp,
                         const LauncherPartitionEntry &candidateSpiffs,
                         bool candidateHasSpiffs,
                         const LauncherPartitionEntry candidateFat[2],
                         const bool candidateHasFat[2]) {
        for (const String &existingLabel : choiceLabels) {
            if (existingLabel == label) return;
        }
        choiceLabels.push_back(label);
        choices.push_back(
            {label,
             [&table,
              &appEntry,
              &spiffsEntry,
              &hasSpiffsEntry,
              fatEntry,
              hasFatEntry,
              candidate,
              candidateApp,
              candidateSpiffs,
              candidateHasSpiffs,
              candidateFat0 = candidateFat[0],
              candidateFat1 = candidateFat[1],
              candidateHasFat0 = candidateHasFat[0],
              candidateHasFat1 = candidateHasFat[1]]() mutable {
                 table = candidate;
                 appEntry = candidateApp;
                 spiffsEntry = candidateSpiffs;
                 hasSpiffsEntry = candidateHasSpiffs;
                 fatEntry[0] = candidateFat0;
                 fatEntry[1] = candidateFat1;
                 hasFatEntry[0] = candidateHasFat0;
                 hasFatEntry[1] = candidateHasFat1;
             }}
        );
    };

    auto addAutoLayoutChoice = [&](const String &label, LauncherPartitionTable candidate) {
        LauncherPartitionEntry candidateApp;
        LauncherPartitionEntry candidateSpiffs;
        LauncherPartitionEntry candidateFat[2];
        bool candidateHasSpiffs = false;
        bool candidateHasFat[2] = {false, false};

        if (!launcherPartitionCreateOtaApp(
                candidate, updateSize, defaultLabel.c_str(), &candidateApp, &error
            )) {
            return;
        }
        if (!prepareDynamicDataPartitions(
                candidate,
                spiffs,
                spiffsSize,
                candidateSpiffs,
                candidateHasSpiffs,
                fat,
                fatSize,
                fatLabel,
                candidateFat,
                candidateHasFat,
                error
            ) ||
            !launcherPartitionValidate(candidate, &error)) {
            return;
        }

        addChoice(
            label, candidate, candidateApp, candidateSpiffs, candidateHasSpiffs, candidateFat, candidateHasFat
        );
    };

    for (const LauncherPartitionEntry &entry : original.entries) {
        if (!isReplaceableOnlineApp(entry) || entry.size < requiredAppPartitionSize) continue;
        LauncherPartitionTable candidate = original;
        LauncherPartitionEntry candidateApp = entry;
        LauncherPartitionEntry candidateSpiffs;
        LauncherPartitionEntry candidateFat[2];
        bool candidateHasSpiffs = false;
        bool candidateHasFat[2] = {false, false};
        if (prepareDynamicDataPartitions(
                candidate,
                spiffs,
                spiffsSize,
                candidateSpiffs,
                candidateHasSpiffs,
                fat,
                fatSize,
                fatLabel,
                candidateFat,
                candidateHasFat,
                error
            ) &&
            launcherPartitionValidate(candidate, &error)) {
            addChoice(
                String("Use ") + entry.label + " partition",
                candidate,
                candidateApp,
                candidateSpiffs,
                candidateHasSpiffs,
                candidateFat,
                candidateHasFat
            );
        }
    }

    std::vector<LauncherPartitionEntry> apps = originalApps;
    std::sort(apps.begin(), apps.end(), [](const LauncherPartitionEntry &a, const LauncherPartitionEntry &b) {
        return a.offset < b.offset;
    });

    for (size_t start = 0; start < apps.size(); ++start) {
        if (apps[start].size >= requiredAppPartitionSize) continue;
        LauncherPartitionTable candidate = original;
        removeEntryByOffset(candidate, apps[start].offset);

        LauncherPartitionEntry candidateApp;
        if (!addManualAppEntry(
                candidate,
                apps[start].subtype,
                apps[start].label,
                apps[start].offset,
                updateSize,
                candidateApp,
                error
            )) {
            continue;
        }

        LauncherPartitionEntry candidateSpiffs;
        LauncherPartitionEntry candidateFat[2];
        bool candidateHasSpiffs = false;
        bool candidateHasFat[2] = {false, false};
        if (prepareDynamicDataPartitions(
                candidate,
                spiffs,
                spiffsSize,
                candidateSpiffs,
                candidateHasSpiffs,
                fat,
                fatSize,
                fatLabel,
                candidateFat,
                candidateHasFat,
                error
            ) &&
            launcherPartitionValidate(candidate, &error)) {
            addChoice(
                String("Repartition ") + apps[start].label + " + free",
                candidate,
                candidateApp,
                candidateSpiffs,
                candidateHasSpiffs,
                candidateFat,
                candidateHasFat
            );
        }
    }

    if (std::any_of(original.entries.begin(), original.entries.end(), isRemovableOnlineInstallData)) {
        for (int removalPass = 0; removalPass < 2 && choices.size() == 1; ++removalPass) {
            const bool removeSpiffs = removalPass == 1;
            LauncherPartitionTable candidate = original;
            if (!removeOnlineInstallDataPartitions(candidate, removeSpiffs)) continue;

            LauncherPartitionEntry candidateApp;
            if (!launcherPartitionCreateOtaApp(
                    candidate, updateSize, defaultLabel.c_str(), &candidateApp, &error
                )) {
                continue;
            }

            LauncherPartitionEntry candidateSpiffs;
            LauncherPartitionEntry candidateFat[2];
            bool candidateHasSpiffs = false;
            bool candidateHasFat[2] = {false, false};
            if (!prepareDynamicDataPartitions(
                    candidate,
                    spiffs,
                    spiffsSize,
                    candidateSpiffs,
                    candidateHasSpiffs,
                    fat,
                    fatSize,
                    fatLabel,
                    candidateFat,
                    candidateHasFat,
                    error
                ) ||
                !launcherPartitionValidate(candidate, &error)) {
                continue;
            }

            addChoice(
                removeSpiffs ? "Remove data + use free" : "Remove FAT data + use free",
                candidate,
                candidateApp,
                candidateSpiffs,
                candidateHasSpiffs,
                candidateFat,
                candidateHasFat
            );
        }
    }

    if (std::any_of(original.entries.begin(), original.entries.end(), isRemovableOnlineInstallData)) {
        for (size_t start = 0; start < apps.size(); ++start) {
            uint32_t rangeEnd = apps[start].offset + apps[start].size;
            for (size_t end = start; end < apps.size(); ++end) {
                if (end > start && apps[end].offset != rangeEnd) break;
                rangeEnd = apps[end].offset + apps[end].size;

                for (int removalPass = 0; removalPass < 2; ++removalPass) {
                    const bool removeSpiffs = removalPass == 1;
                    LauncherPartitionTable candidate = original;
                    for (size_t i = start; i <= end; ++i) removeEntryByOffset(candidate, apps[i].offset);
                    if (!removeOnlineInstallDataPartitions(candidate, removeSpiffs)) continue;

                    String label = String("Remove ") + apps[start].label;
                    if (end > start) label += String("-") + apps[end].label;
                    label += removeSpiffs ? " + all data" : " + FAT data";
                    addAutoLayoutChoice(label, candidate);
                }
            }
        }
    }

    for (size_t start = 0; start < apps.size(); ++start) {
        uint32_t rangeStart = apps[start].offset;
        uint32_t rangeEnd = apps[start].offset + apps[start].size;
        for (size_t end = start; end < apps.size(); ++end) {
            if (end > start && apps[end].offset != rangeEnd) break;
            if (end == start) continue;
            rangeEnd = apps[end].offset + apps[end].size;
            uint32_t usableStart = alignOnlineUp(rangeStart, LAUNCHER_APP_PARTITION_ALIGNMENT);
            if (rangeEnd <= usableStart || rangeEnd - usableStart < requiredAppPartitionSize) continue;

            LauncherPartitionTable candidate = original;
            for (size_t i = start; i <= end; ++i) removeEntryByOffset(candidate, apps[i].offset);

            LauncherPartitionEntry candidateApp;
            if (!addManualAppEntry(
                    candidate,
                    apps[start].subtype,
                    apps[start].label,
                    usableStart,
                    updateSize,
                    candidateApp,
                    error
                )) {
                continue;
            }

            LauncherPartitionEntry candidateSpiffs;
            LauncherPartitionEntry candidateFat[2];
            bool candidateHasSpiffs = false;
            bool candidateHasFat[2] = {false, false};
            if (!prepareDynamicDataPartitions(
                    candidate,
                    spiffs,
                    spiffsSize,
                    candidateSpiffs,
                    candidateHasSpiffs,
                    fat,
                    fatSize,
                    fatLabel,
                    candidateFat,
                    candidateHasFat,
                    error
                ) ||
                !launcherPartitionValidate(candidate, &error)) {
                continue;
            }

            String label = String("Repartition ") + apps[start].label + "-" + apps[end].label;
            addChoice(
                label,
                candidate,
                candidateApp,
                candidateSpiffs,
                candidateHasSpiffs,
                candidateFat,
                candidateHasFat
            );
        }
    }

    const bool needsDataRemoval = choices.size() == 1;
    if (needsDataRemoval &&
        std::any_of(original.entries.begin(), original.entries.end(), isRemovableOnlineInstallData)) {
        for (int removalPass = 0; removalPass < 2 && choices.size() == 1; ++removalPass) {
            const bool removeSpiffs = removalPass == 1;
            for (size_t start = 0; start < apps.size(); ++start) {
                uint32_t rangeStart = apps[start].offset;
                uint32_t rangeEnd = apps[start].offset + apps[start].size;
                for (size_t end = start; end < apps.size(); ++end) {
                    if (end > start && apps[end].offset != rangeEnd) break;
                    rangeEnd = apps[end].offset + apps[end].size;

                    LauncherPartitionTable candidate = original;
                    for (size_t i = start; i <= end; ++i) removeEntryByOffset(candidate, apps[i].offset);
                    if (!removeOnlineInstallDataPartitions(candidate, removeSpiffs)) continue;

                    LauncherPartitionEntry candidateApp;
                    const uint32_t usableStart = alignOnlineUp(rangeStart, LAUNCHER_APP_PARTITION_ALIGNMENT);
                    if (rangeEnd <= usableStart || rangeEnd - usableStart < requiredAppPartitionSize)
                        continue;
                    if (!addManualAppEntry(
                            candidate,
                            apps[start].subtype,
                            apps[start].label,
                            usableStart,
                            updateSize,
                            candidateApp,
                            error
                        )) {
                        continue;
                    }

                    LauncherPartitionEntry candidateSpiffs;
                    LauncherPartitionEntry candidateFat[2];
                    bool candidateHasSpiffs = false;
                    bool candidateHasFat[2] = {false, false};
                    if (!prepareDynamicDataPartitions(
                            candidate,
                            spiffs,
                            spiffsSize,
                            candidateSpiffs,
                            candidateHasSpiffs,
                            fat,
                            fatSize,
                            fatLabel,
                            candidateFat,
                            candidateHasFat,
                            error
                        ) ||
                        !launcherPartitionValidate(candidate, &error)) {
                        continue;
                    }

                    String label = String("Remove ") + apps[start].label;
                    if (end > start) label += String("-") + apps[end].label;
                    label += removeSpiffs ? " + free + all data" : " + free + FAT data";
                    addChoice(
                        label,
                        candidate,
                        candidateApp,
                        candidateSpiffs,
                        candidateHasSpiffs,
                        candidateFat,
                        candidateHasFat
                    );
                }
            }
        }
    }

    choices.push_back({"Cancel", []() {}});
    const int selected = loopOptions(choices);
    if (selected <= 0 || selected == static_cast<int>(choices.size()) - 1) {
        error = "No install target selected";
        return false;
    }

    if (appEntry.offset == 0) {
        error = "Selected install target failed";
        return false;
    }
    return true;
}

bool flashRawRangeFromHttp(
    const String &url, uint32_t sourceOffset, size_t imageSize, const LauncherPartitionEntry &target,
    bool appImage, const char *hwid = nullptr
) {
    pauseInputHandlerTask();
    launcherConsolePrintf(
        "HTTP raw flash begin label=%s target=0x%08X partition=0x%08X source=0x%08X image=0x%08X app=%d\n",
        target.label,
        static_cast<unsigned>(target.offset),
        static_cast<unsigned>(target.size),
        static_cast<unsigned>(sourceOffset),
        static_cast<unsigned>(imageSize),
        appImage
    );
    RawHttpUpdateContext update = {target.offset, target.size, imageSize, 0, appImage, false};
    bool httpOk = false;
    LauncherHttpResponse response;
    constexpr uint8_t maxAttempts = 24;
    for (uint8_t attempt = 0; update.written < imageSize && attempt < maxAttempts; ++attempt) {
        size_t before = update.written;
        const uint32_t requestOffset = sourceOffset + update.written;
        const size_t remaining = imageSize - update.written;
        response = LauncherHttpResponse();
        httpOk = launcherHttpGetRange(
            url.c_str(), requestOffset, remaining, launcherRawUpdateHttpCb, &update, &response, hwid
        );
        if (httpOk && update.written == imageSize) break;
        launcherConsolePrintf(
            "HTTP range retry %u: status=%d len=%lld written=%u/%u advanced=%u\n",
            static_cast<unsigned>(attempt + 1),
            response.status,
            static_cast<long long>(response.content_length),
            static_cast<unsigned>(update.written),
            static_cast<unsigned>(imageSize),
            static_cast<unsigned>(update.written - before)
        );
        if (update.written == before) break;
        launcherDelayMs(500);
    }
    bool complete = update.written == imageSize;
    bool endOk = complete && launcherRawUpdateEnd();
    bool ok = complete && endOk;
    if (!ok) {
        launcherConsolePrintf(
            "HTTP raw flash failed: http_ok=%d complete=%d end_ok=%d status=%d len=%lld written=%u "
            "expected=%u err=%s\n",
            httpOk,
            complete,
            endOk,
            response.status,
            static_cast<long long>(response.content_length),
            static_cast<unsigned>(update.written),
            static_cast<unsigned>(imageSize),
            launcherUpdateLastErrorName()
        );
    }
    resumeInputHandlerTask();
    return ok;
}

bool installFirmwareDynamic(
    const String &fileAddr, const String &file, uint32_t appSize, uint32_t appPartitionSize,
    uint32_t appOffset, bool spiffs, uint32_t spiffsOffset, uint32_t spiffsSize, uint32_t spiffsCopySize,
    bool nb, bool fat, uint32_t fatOffset[2], uint32_t fatSize[2], uint32_t fatCopySize[2],
    String fatLabel[2], const String &installedName
) {
    String error;
    LauncherPartitionTable table;
    if (!launcherPartitionReadCurrent(table, &error)) {
        displayRedStripe(error.length() ? error : "Partition read failed");
        return false;
    }

    size_t updateSize = appSize;
    String hwid = String(launcherWifiMac().c_str());
    if (updateSize == 0) {
        size_t remoteSize = 0;
        if (!getRemoteFileSize(fileAddr, remoteSize, hwid.c_str())) {
            displayRedStripe("Size failed");
            return false;
        }
        if (nb) {
            updateSize = remoteSize;
        } else {
            if (appOffset >= remoteSize) {
                displayRedStripe("Bad app offset");
                return false;
            }
            updateSize = remoteSize - appOffset;
        }
    }
    if (updateSize == 0) {
        displayRedStripe("Invalid app size");
        return false;
    }
    if (appPartitionSize == 0 || appPartitionSize < updateSize) appPartitionSize = updateSize;

    String appLabel = nextOnlineAppLabel(table);
    LauncherPartitionEntry appEntry;
    LauncherPartitionEntry spiffsEntry;
    bool hasSpiffsEntry = false;
    LauncherPartitionEntry fatEntry[2];
    bool hasFatEntry[2] = {false, false};

    if (!selectInstallLayout(
            table,
            appPartitionSize,
            appLabel,
            spiffs,
            spiffsSize,
            fat,
            fatSize,
            fatLabel,
            appEntry,
            spiffsEntry,
            hasSpiffsEntry,
            fatEntry,
            hasFatEntry,
            error
        )) {
        launcherConsolePrintf("Dynamic install layout failed: %s\n", error.c_str());
        displayRedStripe(error.length() ? error : "No install space");
        return false;
    }

    pauseInputHandlerTask();
    bool success = false;
    displayRedStripe("Installing APP");
    prog_handler = 0;
    progressHandler(0, updateSize);
    if (!flashRawRangeFromHttp(fileAddr, nb ? 0 : appOffset, updateSize, appEntry, true, hwid.c_str())) {
        displayRedStripe(String("APP: ") + launcherUpdateLastErrorName());
        goto DONE;
    }

    if (hasSpiffsEntry) {
        if (spiffsCopySize > 0) {
            const uint32_t copySize = spiffsCopySize > spiffsEntry.size ? spiffsEntry.size : spiffsCopySize;
            if (copySize > 0) {
                displayRedStripe("Installing SPIFFS");
                prog_handler = 1;
                progressHandler(0, copySize);
                if (!flashRawRangeFromHttp(
                        fileAddr, spiffsOffset, copySize, spiffsEntry, false, hwid.c_str()
                    )) {
                    displayRedStripe(String("SPIFFS: ") + launcherUpdateLastErrorName());
                    goto DONE;
                }
            }
        }
    }

    for (int i = 0; i < 2; ++i) {
        if (!hasFatEntry[i]) continue;
        if (fatCopySize[i] == 0) continue;
        displayRedStripe("Installing FAT");
        prog_handler = 1;
        progressHandler(0, fatCopySize[i]);
        if (!flashRawRangeFromHttp(
                fileAddr, fatOffset[i], fatCopySize[i], fatEntry[i], false, hwid.c_str()
            )) {
            displayRedStripe(String("FAT: ") + launcherUpdateLastErrorName());
            goto DONE;
        }
    }

    displayRedStripe("Writing table");
    if (!launcherPartitionWriteGeneratedTable(table, &error)) {
        displayRedStripe(error.length() ? error : "Table failed");
        goto DONE;
    }

    displayRedStripe("Setting boot");
    if (!launcherPartitionSetOtaBoot(table, appEntry.subtype, &error)) {
        displayRedStripe(error.length() ? error : "Boot failed");
        goto DONE;
    }

    {
        String installedLabel = String(appEntry.label);
        for (const LauncherAppMetadata &registeredApp : launcherLoadAppRegistry()) {
            if (!launcherPartitionFindByLabel(table, registeredApp.label.c_str())) {
                launcherRemoveAppMetadata(registeredApp.label.c_str());
            }
        }

        LauncherAppMetadata metadata;
        metadata.name = installedName;
        if (metadata.name.isEmpty()) metadata.name = launcherAppNameFromFile(file);
        if (metadata.name.isEmpty()) metadata.name = installedLabel;
        metadata.label = installedLabel;
        launcherSaveAppMetadata(metadata);
        lastInstalledApp = metadata.name;
    }

    saveIntoNVS();
    success = true;

DONE:
    resumeInputHandlerTask();
    if (success) {
        displayRedStripe("Restarting");
        launcherDelayMs(500);
        reboot();
    }
    return success;
}

bool getInfo(String serverUrl, JsonDocument &_doc) {
    if (launcherWifiIsConnected()) {
        pauseInputHandlerTask();
        resetTftDisplay();
        tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
        tft->drawCentreString("Getting info from", tftWidth / 2, tftHeight / 3, 1);
        tft->drawCentreString("LauncherHub", tftWidth / 2, tftHeight / 3 + FM * 9, 1);
        tft->display(false);
        tft->setCursor(18, tftHeight / 3 + FM * 9 * 2);
        const uint8_t maxAttempts = 5;
        for (uint8_t attempt = 0; attempt < maxAttempts; ++attempt) {
            String payload;
            if (launcherHttpGetToString(serverUrl.c_str(), payload)) {
                _doc.clear();
                DeserializationError error = deserializeJson(_doc, payload);
                if (error) {
                    launcherConsolePrintf("[GetInfo] Failed to parse JSON: %s\n", error.c_str());
                    displayRedStripe("JSON Parse Failed");
                    vTaskDelay(1500 / portTICK_PERIOD_MS);
                    _doc.clear();
                    resumeInputHandlerTask();
                    return false;
                }
                launcherConsolePrintf("[GetInfo] Downloaded and parsed json with size: %d\n", _doc.size());
                resumeInputHandlerTask();
                return true;
            }

            launcherConsolePrintf("[GetInfo] HTTP fetch failed: %s\n", serverUrl.c_str());
            tftprint(".", 10);
            vTaskDelay(pdTICKS_TO_MS(500));
        }
    }
    resumeInputHandlerTask();
    return false;
}

/***************************************************************************************
** Function name: GetJsonFromLauncherHub
** Description:   Gets JSON from github server
***************************************************************************************/
bool GetJsonFromLauncherHub(uint8_t page, String order, bool star, String query) {
    String q = "&order_by=" + order;
    q += page > 1 ? "&page=" + String(page) : "";
    q += query.length() > 0 ? "&q=" + String(query) : "";
    q += star ? "&star=1" : "";
#ifdef OTA_EXTRA
    q += OTA_EXTRA;
#endif
    String serverUrl = "https://api.launcherhub.net/firmwares?category=" + String(OTA_TAG) + q;

    if (getInfo(serverUrl, doc)) {
        total_firmware = doc["total"].as<int>();
        num_pages = doc["total"].as<int>() / doc["page_size"].as<int>();
        current_page = page;
        launcherConsolePrintf("GetJsonFromLauncherHub> Loaded %d firmwares\n", total_firmware);
        return true;
    }
    displayRedStripe("Firmware list fetch Failed");
    vTaskDelay(1500 / portTICK_PERIOD_MS);
    return false;
}
JsonDocument getVersionInfo(String fid) {
    JsonDocument versions;
    String serverUrl = "https://api.launcherhub.net/firmwares?fid=" + fid;
    if (!getInfo(serverUrl, versions)) {
        displayRedStripe("Version fetch Failed");
        vTaskDelay(1500 / portTICK_PERIOD_MS);
    }
    return versions;
}

String encodeQueryValue(const String &value) {
    String encoded;
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
            encoded += hex;
        }
    }
    return encoded;
}

void installFirmwareFromManifest(String fid, String version, String installedName) {
    displayRedStripe("Getting install info");

    JsonDocument detail;
    String serverUrl =
        "https://api.launcherhub.net/firmwares?fid=" + fid + "&version=" + encodeQueryValue(version);
    if (!getInfo(serverUrl, detail)) {
        displayRedStripe("Install info failed");
        launcherDelayMs(2000);
        return;
    }

    JsonObject versionObj = detail["version"].as<JsonObject>();
    JsonObject install = versionObj["install"].as<JsonObject>();
    JsonObject app = install["app"].as<JsonObject>();
    JsonArray partitions = install["partitions"].as<JsonArray>();
    String file = versionObj["file"].as<String>();
    if (file.isEmpty() || app.isNull()) {
        displayRedStripe("Bad install info");
        launcherDelayMs(2000);
        return;
    }

    uint32_t appOffset = app["source_offset"] | 0;
    uint32_t appCopySize = app["image_size"] | 0;
    uint32_t appPartitionSize = appCopySize;
    bool nb = appOffset == 0;

    bool spiffs = false;
    uint32_t spiffsOffset = 0;
    uint32_t spiffsSize = 0;
    uint32_t spiffsCopySize = 0;

    bool fat = false;
    uint32_t fatOffset[2] = {0, 0};
    uint32_t fatSize[2] = {0, 0};
    uint32_t fatCopySize[2] = {0, 0};
    String fatLabel[2] = {"sys", "vfs"};
    int fatCount = 0;

    for (JsonObject part : partitions) {
        String type = part["type"].as<String>();
        String subtype = part["subtype"].as<String>();
        if (type == "app" && subtype == "ota") {
            appOffset = part["source_offset"] | appOffset;
            appCopySize = part["copy_size"] | appCopySize;
            appPartitionSize = appCopySize;
            nb = appOffset == 0;
        } else if (type == "data" && subtype == "spiffs") {
            spiffs = true;
            uint32_t declaredSize = part["size"] | 0;
            spiffsOffset = part["source_offset"] | 0;
            spiffsCopySize = part["copy_size"] | 0;
            spiffsSize = declaredSize > onlineLargeSpiffsThreshold() ? onlineUseRemainingSpiffsSize()
                                                                     : onlineDefaultSpiffsSize();
        } else if (type == "data" && subtype == "fat" && fatCount < 2) {
            fat = true;
            fatLabel[fatCount] = part["label"].as<String>();
            if (fatLabel[fatCount].isEmpty()) fatLabel[fatCount] = fatCount == 0 ? "sys" : "vfs";
            uint32_t declaredSize = part["size"] | 0;
            fatOffset[fatCount] = part["source_offset"] | 0;
            uint32_t requestedCopySize = part["copy_size"] | 0;
            LauncherPartitionPayloadPlan payload =
                launcherPartitionFatPayloadPlan(fatLabel[fatCount].c_str(), declaredSize, requestedCopySize);
            fatSize[fatCount] = payload.partitionSize;
            fatCopySize[fatCount] = payload.copySize;
            fatCount++;
        }
    }

    if (appCopySize == 0 || appPartitionSize == 0) {
        displayRedStripe("Invalid app size");
        launcherDelayMs(2000);
        return;
    }

    if (!file.startsWith("https://")) file = M5_SERVER_PATH + file;
    String fileAddr = "https://api.launcherhub.net/download?fid=" + fid + "&file=" + file;
    if (fid == "") fileAddr = file;

    String manifestName = detail["name"].as<String>();
    if (!manifestName.isEmpty()) installedName = manifestName;

    if (!installFirmwareDynamic(
            fileAddr,
            file,
            appCopySize,
            appPartitionSize,
            appOffset,
            spiffs,
            spiffsOffset,
            spiffsSize,
            spiffsCopySize,
            nb,
            fat,
            fatOffset,
            fatSize,
            fatCopySize,
            fatLabel,
            installedName
        )) {
        launcherDelayMs(2500);
    }
}
/***************************************************************************************
** Function name: downloadFirmware
** Description:   Downloads the firmware and save into the SDCard
***************************************************************************************/
void downloadFirmware(String fid, String file_url, String fileName, String folder) { // Adicionar "fid"
    if (!file_url.startsWith("https://")) file_url = M5_SERVER_PATH + file_url;
    String fileAddr = "https://api.launcherhub.net/download?fid=" + fid + "&file=" + file_url;
    if (fid == "") fileAddr = file_url;
    int tries = 0;
    fileName = replaceChars(fileName);
    prog_handler = 2;
    if (!setupSdCard()) {
        displayRedStripe("SDCard Not Found");
        launcherDelayMs(2500);
        return;
    }
    log_i("Download folder before checks: '%s'", folder.c_str());
    if (!folder.endsWith("/")) folder = folder + "/";
    if (!folder.startsWith("/")) folder = "/" + folder;
    log_i("Download folder after checks: '%s'", folder.c_str());
    String folder_name = folder.substring(0, folder.length() - 1);
    if (folder_name.length() > 2) {
        if (!SDM.exists(folder_name)) {
            if (!SDM.mkdir(folder_name)) {
                log_i("Download: Couldn't create folder '%s'\n", folder_name.c_str());
                displayRedStripe("Can't create: '" + folder_name + "'");
                launcherDelayMs(2000);
                return;
            }
        }
    }
    String filePath = folder + fileName + ".bin";

    tft->fillRect(7, 40, tftWidth - 14, 88, BGCOLOR); // Erase the information below the firmware name
    displayRedStripe("Connecting FW");
    File file;
retry:
    file = SDM.open(filePath, FILE_WRITE);
    if (!file) {
        log_i("Download: Couldn't create file %s", filePath.c_str());
        displayRedStripe("Fail creating file.");
        launcherDelayMs(2000);
        return;
    }
    LauncherHttpResponse response;
    displayRedStripe("Downloading FW");
    prog_handler = 2;
    pauseInputHandlerTask();
    FileDownloadContext download = {&file, 0, 0, 0, &response};
    bool ok = launcherHttpGetStream(
        fileAddr.c_str(), fileDownloadCb, &download, &response, "HWID", launcherWifiMac().c_str()
    );
    file.flush();
    file.close();
    resumeInputHandlerTask();

    vTaskDelay(pdTICKS_TO_MS(50));
    file = SDM.open(filePath, FILE_READ);
    size_t sdSize = file ? file.size() : 0;
    if (file) file.close();
    if ((!ok || sdSize <= bufSize) && tries < 1) {
        tries++;
        SDM.remove(filePath);
        goto retry;
    }
    launcherConsolePrintf(
        "HTTP status          = %d\nFile size in get() = %d\nFile size in SD    = %d\nDownloaded bytes   = "
        "%d\n",
        response.status,
        (int)response.content_length,
        sdSize,
        (int)download.downloaded
    );
    if (!ok || (response.content_length > 0 && sdSize != (size_t)response.content_length)) {
        SDM.remove(filePath);
        displayRedStripe("Download FAILED");
        while (!check(SelPress)) yield();
    } else {
        launcherConsolePrintln("File successfully downloaded..");
        displayRedStripe(" Downloaded ");
        while (!check(SelPress)) yield();
    }
    wakeUpScreen();
}
/***************************************************************************************
** Function name: installExtFirmware
** Description:   installs External Firmware using OTA grabbing file information from url
***************************************************************************************/
bool installExtFirmware(String url) {
    size_t file_size;
    bool spiffs = 0;
    uint32_t spiffs_offset = 0;
    uint32_t spiffs_size = 0;
    bool nb = 1; // File without bootloader an partitions
    bool fat = 0;
    uint32_t fat_offset[2] = {0};
    uint32_t fat_size[2] = {0};
    uint8_t bytes[16];
    if (!url.startsWith("https://")) {
        displayRedStripe("Invalid link");
        return false;
    }
    displayRedStripe("Getting file info");
    LauncherHttpResponse response;
    RangeBufferContext range = {buff, bufSize, 0};
    if (!launcherHttpGetRange(url.c_str(), 32768, 416, rangeBufferCb, &range, &response) ||
        response.status != 206) {
        displayRedStripe("File not found");
        return false;
    }
    if (!parseContentRangeTotal(response.content_range, file_size)) return false;

    // Check if it is a valid partition table
    size_t PartitionSize = 0;
    size_t PartitionOffset = 0x10000;
    if (buff[0] == 0xAA) {
        nb = 0;                                    // File with bootloader an partitions
        for (int i = 0x0; i <= 0x1A0; i += 0x20) { // Partition
            memcpy(bytes, &buff[i], 16);

            // https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/partition-tables.html
            // -> spiffs (0x82) is for SPIFFS Filesystem.

            // if (bytes[3] == 0xFF) Serial.println(": ------- END of Table ------- |");
            if (bytes[3] == 0x00 || (bytes[3] >= 0x10 && bytes[3] <= 0x1F)) {
                launcherConsolePrintln(": Ota or Factory partition |");
                if (bytes[0x0A] > 0 && PartitionSize == 0) {
                    PartitionSize = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) |
                                    bytes[0x0C]; // Write the size of app0 partition
                    PartitionOffset = (bytes[0x06] << 16) | (bytes[0x07] << 8) |
                                      bytes[0x08]; // Write the offset of app0 partition
                }
            }
            // if (bytes[3] == 0x01) Serial.println(": PHY inicialization partition |");
            //  if (bytes[3] == 0x02) Serial.println(": NVS partition                |");
            //  if (bytes[3] == 0x03) Serial.println(": Coredump partition           |");
            //  if (bytes[3] == 0x04) Serial.println(": NVSkeys partition            |");
            //  if (bytes[3] == 0x05) Serial.println(": Efuse partition              |");
            //  if (bytes[3] == 0x06) Serial.println(": Undefined partition          |");
            // if (bytes[3] >= 0x10 && bytes[3] <= 0x1F)
            //     Serial.println(": OTA partition                |");
            // if (bytes[3] == 0x20) Serial.println(": TEST partition               |");
            if (bytes[3] == 0x81) {
                launcherConsolePrintln(": FAT partition                |");
                fat = true;
                int a = 0;
                if (fat_offset[0] != 0) a = 1;
                fat_offset[a] = (bytes[0x06] << 16) | (bytes[0x07] << 8) |
                                bytes[0x08]; // Write the offset of FAT partition
                bytes[0x0C] = 0;
                fat_size[a] =
                    (bytes[0x0A] << 16) | (bytes[0x0B] << 8) | bytes[0x0C]; // Write the size of FAT partition
            }
            if (bytes[3] == 0x82 || bytes[3] == 0x83) {
                launcherConsolePrintln(": Spiffs/LittleFs partition    |");
                spiffs = true;
                spiffs_offset = (bytes[0x06] << 16) | (bytes[0x07] << 8) |
                                bytes[0x08]; // Write the offset of spiffs partition
                bytes[0x0C] = 0;
                spiffs_size = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) |
                              bytes[0x0C]; // Write the size of spiffs partition
            }
        }
        size_t temp_size = 0;
        if (file_size < MAX_APP || PartitionSize <= MAX_APP) {
            temp_size = PartitionSize;
            temp_size += PartitionOffset;
            if (file_size <= temp_size) {         // Check if the file is smaller than the app0 partition
                PartitionSize = file_size;        // gets file size
                PartitionSize -= PartitionOffset; // subtracts bootloader, partitions and other junks
            } else {
                PartitionSize = PartitionSize; // if file is greater then app0 partition+junk, it will
                                               // limit to app0 partition size
            }
        }
        // Check if there is room for spiffs in the file
        if (file_size < spiffs_offset) {
            launcherConsolePrintf(
                "\nError: file doesn't reach spiffs offset %d, to read spiffs.", spiffs_offset, HEX
            );
        } else {
            launcherConsolePrintln("Preparing to copy spiffs...");
            // check size of the Spiffs Partition, if it fits in the launcher
            // If it is larger the the Launcher Spiffs Partition, cut it to the limit
            if (spiffs_size > MAX_SPIFFS) {
                spiffs_size = MAX_SPIFFS;
                temp_size = spiffs_offset + spiffs_size;
                if (file_size <= temp_size) { spiffs_size = file_size - spiffs_offset; }
                launcherConsolePrintf("\nTotal spiffs size after crop: %X\n", spiffs_size);
            }
        }
    }
    launcherConsolePrintf(
        "url: %s"
        "\nPartitionSize: 0x%x, PartitionOffset: 0x%x,"
        "\nspiffs: %d, spiffs_offset: 0x%x, spiffs_size: 0x%x, "
        "\nnb: %d,"
        "\nfat: %d, fat_offset: 0x%x, fat_size: 0x%x",
        url.c_str(),
        PartitionSize,
        PartitionOffset,
        spiffs,
        spiffs_offset,
        spiffs_size,
        nb,
        fat,
        fat_offset,
        fat_size
    );
    installFirmware(
        "",
        url,
        PartitionSize,
        PartitionOffset,
        spiffs,
        spiffs_offset,
        spiffs_size,
        nb,
        fat,
        fat_offset,
        fat_size,
        "External OTA"
    );
    return true;
}

/***************************************************************************************
 ** Function name: clearCoredump
 ** Description:   As some programs may generate core dumps,
                   and others try to report them thinking that they wrote it,
                   this function will clear it to avoid confusion.
****************************************************************************************/
#include <esp_flash.h>
bool clearOnlineCoredump() {
    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "coredump");
    launcherConsolePrintf("Coredump partition address: 0x%08X\n", partition ? partition->address : 0);
    if (!partition) {
        launcherConsolePrintln("Failed to find coredump partition");
        log_e("Failed to find coredump partition");
        return false;
    }
    log_i("Erasing coredump partition at address 0x%08X, size %d bytes", partition->address, partition->size);

    // erase all coredump partition
    esp_err_t err = esp_flash_erase_region(NULL, partition->address, partition->size);
    if (err != ESP_OK) {
        launcherConsolePrintln("Failed to erase coredump partition");
        log_e("Failed to erase coredump partition: %s", esp_err_to_name(err));
        return false;
    }
    launcherConsolePrintln("Coredump partition cleared successfully");
    log_e("Coredump partition cleared successfully");
    return true;
}

/***************************************************************************************
** Function name: installFirmware
** Description:   installs Firmware using OTA
***************************************************************************************/
void installFirmware( // adicionar "fid"
    String fid, String file, uint32_t app_size, uint32_t app_offset, bool spiffs, uint32_t spiffs_offset, uint32_t spiffs_size, bool nb,
    bool fat, uint32_t fat_offset[2], uint32_t fat_size[2], String installedName
) {
    if (!file.startsWith("https://")) file = M5_SERVER_PATH + file;
    String fileAddr = "https://api.launcherhub.net/download?fid=" + fid + "&file=" + file;
    if (fid == "") fileAddr = file;

    // Release RAM Memory from Json Objects
    if (askSpiffs == false) spiffs = false; // avoid Spiffs
    if (spiffs && askSpiffs) {
        options = {
            {"SPIFFS No",  [&]() { spiffs = false; }},
            {"SPIFFS Yes", [&]() { spiffs = true; } },
        };
        loopOptions(options);
    }

    if (spiffs && spiffs_size > MAX_SPIFFS) spiffs_size = MAX_SPIFFS;
    if (app_size > MAX_APP) app_size = MAX_APP;
    if (app_size > MAX_APP) app_size = MAX_APP;

    if (fat && fat_size[0] > MAX_FAT_vfs && fat_size[1] == 0) fat_size[0] = MAX_FAT_vfs;
    else if (fat && fat_size[0] > MAX_FAT_sys) fat_size[0] = MAX_FAT_sys;
    if (fat && fat_size[1] > MAX_FAT_vfs) fat_size[1] = MAX_FAT_vfs;
    uint32_t fat_copy_size[2] = {fat_size[0], fat_size[1]};
    String fat_label[2] = {"sys", "vfs"};

    tft->fillRect(7, 40, tftWidth - 14, 88, BGCOLOR); // Erase the information below the firmware name
    displayRedStripe("Connecting FW");

    if (!installFirmwareDynamic(
            fileAddr,
            file,
            app_size,
            app_size,
            app_offset,
            spiffs,
            spiffs_offset,
            spiffs_size,
            spiffs_size,
            nb,
            fat,
            fat_offset,
            fat_size,
            fat_copy_size,
            fat_label,
            installedName
        )) {
        launcherDelayMs(2500);
    }
    return;

    /* Install App */
    prog_handler = 0;
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
    progressHandler(0, 500);
    pauseInputHandlerTask();
    size_t updateSize = app_size;
    HttpUpdateContext appUpdate = {LAUNCHER_UPDATE_APP, updateSize, 0, false};
    bool success = false;
    String hwid = String(launcherWifiMac().c_str());
    if (nb && updateSize == 0 && !getRemoteFileSize(fileAddr, updateSize, hwid.c_str())) goto SAIR;
    appUpdate.expected = updateSize;
    success =
        nb ? launcherHttpGetRange(
                 fileAddr.c_str(), 0, updateSize, launcherUpdateHttpCb, &appUpdate, nullptr, hwid.c_str()
             )
           : launcherHttpGetRange(
                 fileAddr.c_str(),
                 app_offset,
                 updateSize,
                 launcherUpdateHttpCb,
                 &appUpdate,
                 nullptr,
                 hwid.c_str()
             );
    if (success) success = launcherUpdateEnd();
    if (!success) {
        displayRedStripe(String("OTA: ") + launcherUpdateLastErrorName());
        goto SAIR;
    }
    displayRedStripe("Removing Coredump");
    clearOnlineCoredump();

    // Do not request to api.launcherhub.net a second time, go straight to the file
    // Requests must be done to "file" link directly
    if (spiffs) {
        prog_handler = 1;
        tft->fillRect(5, 60, tftWidth - 10, 16, ALCOLOR);
        setTftDisplay(5, 60, WHITE, FM, ALCOLOR);

        tft->println(" Preparing SPIFFS");
        // Format Spiffs partition
        if (!SPIFFS.begin(true)) {
            displayRedStripe("Fail to start SPIFFS");
            launcherDelayMs(2500);
        } else {
            displayRedStripe("Formatting SPIFFS");
            SPIFFS.format();
            SPIFFS.end();
        }
        displayRedStripe("Connecting SPIFFs");

        // Install Spiffs
        progressHandler(0, 500);
        HttpUpdateContext spiffsUpdate = {LAUNCHER_UPDATE_SPIFFS, spiffs_size, 0, false};
        bool spiffsOk = launcherHttpGetRange(
                            file.c_str(), spiffs_offset, spiffs_size, launcherUpdateHttpCb, &spiffsUpdate
                        ) &&
                        launcherUpdateEnd();
        if (!spiffsOk) {
            displayRedStripe("SPIFFS Failed");
            launcherDelayMs(2500);
        }
    }

#if !defined(PART_04MB)
    if (fat) {
        // eraseFAT();
        int FAT = U_FAT_vfs;
        if (fat_size[1] > 0) FAT = U_FAT_sys;
        for (int i = 0; i < 2; i++) {
            if (fat_size[i] > 0) {
                if ((FAT - i * 100) == 400) {
                    if (!installFAT_OTA(file, fat_offset[i], fat_size[i], "sys")) {
                        displayRedStripe("FAT Failed");
                        launcherDelayMs(2500);
                    }
                } else {
                    if (!installFAT_OTA(file, fat_offset[i], fat_size[i], "vfs")) {
                        displayRedStripe("FAT Failed");
                        launcherDelayMs(2500);
                    }
                }
            }
        }
    }
#endif

Sucesso:
    if (!installedName.isEmpty()) {
        lastInstalledApp = installedName;
        saveIntoNVS();
    }
    reboot();

// Só chega aqui se der errado
SAIR:
    resumeInputHandlerTask();
    launcherDelayMs(2000);
}

/***************************************************************************************
** Function name: installFAT_OTA
** Description:   install FAT partition OverTheAir
***************************************************************************************/
bool installFAT_OTA(String file, uint32_t offset, uint32_t size, const char *label) {
    prog_handler = 1; // review

    tft->fillRect(7, 40, tftWidth - 14, 88, BGCOLOR); // Erase the information below the firmware name
    displayRedStripe("Connecting FAT");

    LauncherUpdateTarget target =
        strcmp(label, "sys") == 0 ? LAUNCHER_UPDATE_FAT_SYS : LAUNCHER_UPDATE_FAT_VFS;
    HttpUpdateContext fatUpdate = {target, size, 0, false};
    displayRedStripe("Installing FAT");
    pauseInputHandlerTask();
    bool ok = launcherHttpGetRange(file.c_str(), offset, size, launcherUpdateHttpCb, &fatUpdate) &&
              launcherUpdateEnd();
    resumeInputHandlerTask();
    vTaskDelay(pdTICKS_TO_MS(500));
    return ok;
}

#endif
