#include "onlineLauncher.h"
#include "app_registry.h"
#include "display.h"
#include "idf/idf_http_client.h"
#include "idf/idf_update.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "partition_install_layout.h"
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
    if (wrote != writeLen) { return false; }
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
    if (wrote != writeLen) { return false; }
    updateCtx->written += wrote;
    progressHandler(updateCtx->written, updateCtx->expected);
    return true;
}

bool flashRawRangeFromHttp(
    const String &url, uint32_t sourceOffset, size_t imageSize, const LauncherPartitionEntry &target,
    bool appImage, const char *hwid = nullptr
) {
    pauseInputHandlerTask();
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
        if (update.written == before) break;
        launcherDelayMs(500);
    }
    bool complete = update.written == imageSize;
    bool endOk = complete && launcherRawUpdateEnd();
    bool ok = complete && endOk;
    resumeInputHandlerTask();
    return ok;
}

bool installFirmwareDynamic(
    const String &fileAddr, const String &file, uint32_t appSize, uint32_t appPartitionSize,
    uint32_t appOffset, bool spiffs, uint32_t spiffsOffset, uint32_t spiffsSize, uint32_t spiffsCopySize,
    bool nb, std::vector<LauncherInstallFatPartition> &fatPartitions, const String &installedName
) {
    String error;
    LauncherPartitionTable table;
    if (!launcherPartitionReadCurrent(table, &error)) {
        displayRedStripe(error.length() ? error : "Partition read failed");
        launcherDelayMs(2000);
        return false;
    }

    size_t updateSize = appSize;
    String hwid = String(launcherWifiMac().c_str());
    if (updateSize == 0) {
        size_t remoteSize = 0;
        if (!getRemoteFileSize(fileAddr, remoteSize, hwid.c_str())) {
            displayRedStripe("Size failed");
            launcherDelayMs(2000);
            return false;
        }
        if (nb) {
            updateSize = remoteSize;
        } else {
            if (appOffset >= remoteSize) {
                displayRedStripe("Bad app offset");
                launcherDelayMs(2000);
                return false;
            }
            updateSize = remoteSize - appOffset;
        }
    }
    if (updateSize == 0) {
        displayRedStripe("Invalid app size");
        launcherDelayMs(2000);
        return false;
    }
    if (appPartitionSize == 0 || appPartitionSize < updateSize) appPartitionSize = updateSize;

    String labelSource = installedName;
    if (labelSource.isEmpty()) labelSource = launcherAppNameFromFile(file);
    String appLabel = launcherPartitionNextAppLabel(table, labelSource);
    LauncherPartitionEntry appEntry;
    LauncherPartitionEntry spiffsEntry;
    bool hasSpiffsEntry = false;

    if (!launcherSelectInstallLayout(
            table,
            appPartitionSize,
            appLabel,
            spiffs,
            spiffsSize,
            fatPartitions,
            appEntry,
            spiffsEntry,
            hasSpiffsEntry,
            error
        )) {
        displayRedStripe(error.length() ? error : "No install space");
        launcherDelayMs(2000);
        return false;
    }

    pauseInputHandlerTask();
    bool success = false;
    displayRedStripe("Installing APP");
    prog_handler = 0;
    progressHandler(0, updateSize);
    if (!flashRawRangeFromHttp(fileAddr, nb ? 0 : appOffset, updateSize, appEntry, true, hwid.c_str())) {
        displayRedStripe(String("APP: ") + launcherUpdateLastErrorName());
        launcherDelayMs(2000);
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
                    launcherDelayMs(2000);
                    goto DONE;
                }
            }
        }
    }

    for (const LauncherInstallFatPartition &fatPartition : fatPartitions) {
        if (!fatPartition.hasEntry) continue;
        if (fatPartition.copySize == 0) continue;
        displayRedStripe("Installing FAT");
        prog_handler = 1;
        progressHandler(0, fatPartition.copySize);
        if (!flashRawRangeFromHttp(
                fileAddr,
                fatPartition.sourceOffset,
                fatPartition.copySize,
                fatPartition.entry,
                false,
                hwid.c_str()
            )) {
            displayRedStripe(String("FAT: ") + launcherUpdateLastErrorName());
            launcherDelayMs(2000);
            goto DONE;
        }
    }

    displayRedStripe("Writing table");
    if (!launcherPartitionWriteGeneratedTable(table, &error)) {
        displayRedStripe(error.length() ? error : "Table failed");
        launcherDelayMs(2000);
        goto DONE;
    }

    displayRedStripe("Setting boot");
    if (!launcherPartitionSetOtaBoot(table, appEntry.subtype, &error)) {
        displayRedStripe(error.length() ? error : "Boot failed");
        launcherDelayMs(2000);
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
        for (const LauncherInstallFatPartition &fatPartition : fatPartitions) {
            if (fatPartition.hasEntry) metadata.fatLabels.push_back(String(fatPartition.entry.label));
        }
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
                    displayRedStripe("JSON Parse Failed");
                    vTaskDelay(1500 / portTICK_PERIOD_MS);
                    _doc.clear();
                    resumeInputHandlerTask();
                    return false;
                }
                resumeInputHandlerTask();
                return true;
            }

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

    std::vector<LauncherInstallFatPartition> fatPartitions;

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
            spiffsSize = declaredSize > LAUNCHER_DEFAULT_SPIFFS_THRESHOLD
                             ? LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE
                             : LAUNCHER_DEFAULT_SPIFFS_SIZE;
        } else if (type == "data" && subtype == "fat") {
            LauncherInstallFatPartition fatPartition;
            fatPartition.label = part["label"].as<String>();
            if (fatPartition.label.isEmpty()) { fatPartition.label = fatPartitions.empty() ? "sys" : "vfs"; }
            uint32_t declaredSize = part["size"] | 0;
            fatPartition.sourceOffset = part["source_offset"] | 0;
            uint32_t requestedCopySize = part["copy_size"] | 0;
            LauncherPartitionPayloadPlan payload =
                launcherPartitionFatPayloadPlan(fatPartition.label.c_str(), declaredSize, requestedCopySize);
            fatPartition.partitionSize = payload.partitionSize;
            fatPartition.copySize = payload.copySize;
            fatPartitions.push_back(fatPartition);
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
            fatPartitions,
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
    displayRedStripe("Preparing..");
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
    bool nb = 1;
    std::vector<LauncherInstallFatPartition> fatPartitions;
    uint8_t bytes[16];
    if (!url.startsWith("https://")) {
        displayRedStripe("Invalid link");
        launcherDelayMs(2000);
        return false;
    }
    displayRedStripe("Getting file info");
    LauncherHttpResponse response;
    RangeBufferContext range = {buff, bufSize, 0};
    if (!launcherHttpGetRange(url.c_str(), 32768, 416, rangeBufferCb, &range, &response) ||
        response.status != 206) {
        displayRedStripe("File not found");
        launcherDelayMs(2000);
        return false;
    }
    if (!parseContentRangeTotal(response.content_range, file_size)) return false;

    size_t PartitionSize = 0;
    size_t PartitionOffset = 0x10000;
    if (buff[0] == 0xAA) {
        nb = 0;
        for (int i = 0x0; i <= 0x1A0; i += 0x20) {
            memcpy(bytes, &buff[i], 16);

            if (bytes[3] == 0x00 || (bytes[3] >= 0x10 && bytes[3] <= 0x1F)) {
                if (bytes[0x0A] > 0 && PartitionSize == 0) {
                    PartitionSize = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) | bytes[0x0C];
                    PartitionOffset = (bytes[0x06] << 16) | (bytes[0x07] << 8) | bytes[0x08];
                }
            }
            if (bytes[3] == 0x81) {
                LauncherInstallFatPartition fatPartition;
                if (fatPartitions.empty()) fatPartition.label = "sys";
                else if (fatPartitions.size() == 1) fatPartition.label = "vfs";
                else fatPartition.label = String("fat") + String(fatPartitions.size());
                fatPartition.sourceOffset = (bytes[0x06] << 16) | (bytes[0x07] << 8) | bytes[0x08];
                bytes[0x0C] = 0;
                uint32_t declaredSize = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) | bytes[0x0C];
                LauncherPartitionPayloadPlan payload =
                    launcherPartitionFatPayloadPlan(fatPartition.label.c_str(), declaredSize, declaredSize);
                fatPartition.partitionSize = payload.partitionSize;
                fatPartition.copySize = payload.copySize;
                fatPartitions.push_back(fatPartition);
            }
            if (bytes[3] == 0x82 || bytes[3] == 0x83) {
                spiffs = true;
                spiffs_offset = (bytes[0x06] << 16) | (bytes[0x07] << 8) | bytes[0x08];
                bytes[0x0C] = 0;
                spiffs_size = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) | bytes[0x0C];
            }
        }
        size_t temp_size = 0;
        if (file_size < MAX_APP || PartitionSize <= MAX_APP) {
            temp_size = PartitionSize;
            temp_size += PartitionOffset;
            if (file_size <= temp_size) {
                PartitionSize = file_size;
                PartitionSize -= PartitionOffset;
            }
        }
        if (file_size >= spiffs_offset && spiffs_size > MAX_SPIFFS) {
            spiffs_size = MAX_SPIFFS;
            temp_size = spiffs_offset + spiffs_size;
            if (file_size <= temp_size) { spiffs_size = file_size - spiffs_offset; }
        }
    }
    installFirmware(
        "",
        url,
        PartitionSize,
        PartitionOffset,
        spiffs,
        spiffs_offset,
        spiffs_size,
        nb,
        fatPartitions,
        "External OTA"
    );
    return true;
}

/***************************************************************************************
** Function name: installFirmware
** Description:   installs Firmware using OTA
***************************************************************************************/
void installFirmware( // adicionar "fid"
    String fid, String file, uint32_t app_size, uint32_t app_offset, bool spiffs, uint32_t spiffs_offset, uint32_t spiffs_size, bool nb,
    std::vector<LauncherInstallFatPartition> &fatPartitions, String installedName
) {
    if (!file.startsWith("https://")) file = M5_SERVER_PATH + file;
    String fileAddr = "https://api.launcherhub.net/download?fid=" + fid + "&file=" + file;
    if (fid == "") fileAddr = file;

    uint32_t spiffsCopySize = spiffs_size;

    // Release RAM Memory from Json Objects
    if (askSpiffs == false) spiffsCopySize = 0;
    if (spiffs && askSpiffs && spiffsCopySize > 0) {
        bool copySpiffs = true;
        options = {
            {"SPIFFS No",  [&]() { copySpiffs = false; }},
            {"SPIFFS Yes", [&]() { copySpiffs = true; } },
        };
        loopOptions(options);
        if (!copySpiffs) spiffsCopySize = 0;
    }

    if (spiffs && spiffs_size > MAX_SPIFFS) spiffs_size = MAX_SPIFFS;
    if (app_size > MAX_APP) app_size = MAX_APP;
    if (app_size > MAX_APP) app_size = MAX_APP;

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
            spiffsCopySize,
            nb,
            fatPartitions,
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
        launcherDelayMs(2000);
        goto SAIR;
    }
    displayRedStripe("Post Install Cleanup");
    launcherClearCoredump();

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
    for (const LauncherInstallFatPartition &fatPartition : fatPartitions) {
        if (fatPartition.copySize > 0) {
            if (!installFAT_OTA(
                    file, fatPartition.sourceOffset, fatPartition.copySize, fatPartition.label.c_str()
                )) {
                displayRedStripe("FAT Failed");
                launcherDelayMs(2500);
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
