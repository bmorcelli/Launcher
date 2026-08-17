#include "onlineLauncher.h"
#include "app_registry.h"
#include "display.h"
#include "idf/idf_http_client.h"
#include "idf/idf_update.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "install_shared.h"
#include "littlefs_patch.h"
#include "mykeyboard.h"
#include "partition_install_layout.h"
#include "partition_table_model.h"
#include "powerSave.h"
#include "ram_profile.h"
#include "sd_functions.h"
#include "settings.h"
#include "utils.h"
#include <algorithm>
#include <esp_ota_ops.h>
#include <globals.h>

#define M5_SERVER_PATH "https://m5burner-cdn.m5stack.com/firmware/"

constexpr int kWifiConnectAttempts = 20;

/***************************************************************************************
** Function name: wifiConnect
** Description:   Connects to wifiNetwork
***************************************************************************************/
bool wifiConnect(const String &ssid, int encryptation, bool isAP) {
    RAM_LOG(isAP ? "wifiConnect-ap-start" : "wifiConnect-sta-start");
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
                    launcherDelayMs(0);
                    return false;
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

        resetTftDisplay(10, 10, FGCOLOR, _fp);
        tft->fillScreen(BGCOLOR);
        tftprint("Connecting to: " + ssid + ".", 10);
        tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);

        int count = 0;
        LauncherWifiConnectState connectState = LauncherWifiConnectState::Pending;
        RAM_LOG("before-wifi-connect-status");
        while (connectState != LauncherWifiConnectState::Connected) {
            connectState = launcherWifiConnectStatus(ssid.c_str(), pwd.c_str(), 500);
            if (connectState == LauncherWifiConnectState::Connected) break;
            if (connectState == LauncherWifiConnectState::WrongPassword) {
                displayError("Wrong Password");
                wrongPass = true;
                goto Retry;
            }
            vTaskDelay(500 / portTICK_PERIOD_MS);
            tftprint(".", 10);
            count++;
            if (connectState == LauncherWifiConnectState::Failed || count > kWifiConnectAttempts) {
                options = {
                    {"Retry",     [&]() { yield(); }            },
                    {"Main Menu", [&]() { returnToMenu = true; }},
                };
                loopOptions(options);
                if (!returnToMenu) goto Retry;
                launcherDelayMs(0);
                return false;
            }
            tft->display(false);
        }
    } else { // Running in Access point mode
#if !CONFIG_ESP_HOSTED_ENABLED
        launcherWifiStop();
        vTaskDelay(50 / portTICK_PERIOD_MS);
#endif
        RAM_LOG("before-wifi-start-ap");
        launcherWifiStartAp("Launcher", "", 6, 4);
        vTaskDelay(250 / portTICK_PERIOD_MS);
        launcherConsolePrintf("IP: %s\n", launcherWifiApIp().c_str());
    }
    launcherDelayMs(0);
    RAM_LOG("wifiConnect-end");
    return isAP || launcherWifiIsConnected();
}
bool connectWifi() {
    RAM_LOG("connectWifi-start");
    displayRedStripe("Scanning...");
#if CONFIG_ESP_HOSTED_ENABLED
    launcherWifiStop();
#endif
    std::vector<LauncherWifiAp> networks;
    int nets = launcherWifiScan(networks);
    // Serial.printf("connectWifi: scan returned %d networks\n", nets);
    if (nets < 0) {
        displayError("WiFi scan failed");
        return false;
    }
    for (int i = 0; i < nets; i++) {
        String networkSsid = networks[i].ssid.c_str();
        if (networkSsid.isEmpty()) continue;
        String knownPwd;
        if (!getWifiCredential(networkSsid, knownPwd)) continue;
        launcherConsolePrintf("Auto-connecting to saved SSID: %s\n", networkSsid.c_str());
        if (wifiConnect(networkSsid, static_cast<int>(networks[i].authmode))) return true;
        if (returnToMenu) return false;
    }
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
    return launcherWifiIsConnected();
}

bool ensureWifiConnected(const String &ssid, int encryptation, bool isAP) {
    RAM_LOG("ensureWifiConnected-start");
    if (launcherWifiIsConnected() && !isAP) return true;
    if (isAP) return wifiConnect(ssid, encryptation, true);
    if (!ssid.isEmpty()) return wifiConnect(ssid, encryptation, false);
    return connectWifi();
}
/***************************************************************************************
** Function name: ota_function
** Description:   Start OTA function
***************************************************************************************/
void ota_function() {
#ifndef DISABLE_OTA
    if (!hostedWifiAvailable) {
        displayError("ESP-Hosted unavailable: no WiFi", true);
        return;
    }
    RAM_LOG("ota-start");
    bool fav = false;
    bool upd = false;
    if (ensureWifiConnected()) {
        // Debug
        // Serial.printf("Favorite size: %d\n", favorite.size());
        // serializeJsonPretty(favorite, Serial);
        // Debug
        String dwnJsonPath = dwn_path;
        if (!dwnJsonPath.startsWith("/")) dwnJsonPath = "/" + dwnJsonPath;
        if (!dwnJsonPath.endsWith("/")) dwnJsonPath += "/";
        dwnJsonPath += "downloaded.json";
        bool hasDownloads = sdcardMounted && SDM.exists(dwnJsonPath);
        if (favorite.size() > 0 || hasDownloads) {
            options.clear();
            options.push_back({"OTA List", [&]() {
                                   fav = false;
                                   upd = false;
                               }});
            if (favorite.size() > 0) options.push_back({"Favorite List", [&]() { fav = true; }});
            if (hasDownloads) options.push_back({"Check for Updates", [&]() { upd = true; }});
            options.push_back({"Main Menu", [=]() { returnToMenu = true; }});
            loopOptions(options);
        }
        if (returnToMenu) return;
        if (upd) {
            if (checkForUpdates()) loopFirmware(true);
        } else if (fav) {
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
    if (inputHandlerPauseDepth == 0) {
        // Time spent suspended is not idle time. checkPowerSaveTime() lives in the very
        // task we froze, so the screen-off deadline went on ageing with nothing able to
        // act on it; the first tick after the resume finds it long past and blanks the
        // display just as the outcome — often an error — reaches the screen.
        wakeUpScreen();
        vTaskResume(xHandle);
    }
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
    String activeUrl = url;
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        LauncherHttpResponse response;
        bool ok = launcherHttpGetRange(activeUrl.c_str(), 0, 1, discardHttpCb, nullptr, &response, hwid);
        if (ok && response.status == 206) return parseContentRangeTotal(response.content_range, size);
        // A redirect the device can't follow is api.launcherhub.net's answer, not a
        // hiccup on the CDN side; fall back to /proxy (server-side fetch) once
        // instead of failing the whole install before it even starts.
        const bool shouldTryProxy =
            activeUrl.indexOf("/download?") >= 0 &&
            (response.status == 0 || (response.status >= 300 && response.status < 400));
        if (attempt == 0 && shouldTryProxy) {
            activeUrl.replace("/download?", "/proxy?");
            launcherConsolePrintf("Redirect not followed, falling back to /proxy for size probe\n");
            continue;
        }
        return false;
    }
    return false;
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

    if (download->downloaded == 0) RAM_LOG("fileDownloadCb-first-chunk");

    // On the first chunk, content_length is already populated by fetch_headers.
    // Use it to initialize the progress bar with the real file size.
    if (download->expected == 0 && download->response && download->response->content_length > 0) {
        download->expected = static_cast<size_t>(download->response->content_length);
        progressHandler(0, download->expected);
    }

    size_t totalWrote = 0;
    constexpr size_t sdWriteChunk = 512;
    while (totalWrote < len) {
        const size_t part = min(sdWriteChunk, len - totalWrote);
        size_t wrote = download->file->write(data + totalWrote, part);
        if (wrote != part) {
            download->file->flush();
            vTaskDelay(pdMS_TO_TICKS(20));
            wrote = download->file->write(data + totalWrote, part);
        }
        if (wrote != part) {
            launcherConsolePrintf(
                "Download: SD write failed at pos 0x%06X (%u/%u bytes)\n",
                (unsigned)(download->downloaded + totalWrote),
                (unsigned)wrote,
                (unsigned)part
            );
            return false;
        }
        totalWrote += wrote;
    }
    download->downloaded += totalWrote;

    if (download->expected > 0) {
        if (download->progressTick >= 10) {
            download->file->flush();
            vTaskDelay(pdMS_TO_TICKS(2));
            progressHandler(download->downloaded, download->expected);
            RAM_LOG("fileDownloadCb-progress");
            vTaskDelay(pdMS_TO_TICKS(2));
            download->progressTick = 0;
        } else {
            download->progressTick++;
        }
    }
    return true;
}

struct RawHttpUpdateContext {
    uint32_t address;
    size_t partitionSize;
    size_t skipRemaining;
    size_t expected;
    size_t written;
    bool appImage;
    bool started;
};

bool launcherRawUpdateHttpCb(const uint8_t *data, size_t len, void *ctx) {
    RawHttpUpdateContext *updateCtx = static_cast<RawHttpUpdateContext *>(ctx);
    if (!updateCtx) return false;
    if (updateCtx->skipRemaining > 0) {
        const size_t skip = min(len, updateCtx->skipRemaining);
        data += skip;
        len -= skip;
        updateCtx->skipRemaining -= skip;
        if (len == 0) return true;
    }
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
    bool appImage, const char *hwid = nullptr, String *errorOut = nullptr
) {
    pauseInputHandlerTask();
    RawHttpUpdateContext update = {target.offset, target.size, sourceOffset, imageSize, 0, appImage, false};
    LauncherHttpResponse response;
    RAM_LOG("flashRawRangeFromHttp-stream");
    bool httpOk =
        launcherHttpGetStream(url.c_str(), launcherRawUpdateHttpCb, &update, &response, "HWID", hwid);
    bool complete = update.written == imageSize;
    bool endOk = complete && launcherRawUpdateEnd();
    bool ok = complete && endOk;
    if (ok && !appImage) {
        String patchError;
        if (!launcherPatchReducedLittlefsSuperblocks(target, &patchError)) {
            launcherConsolePrintf(
                "LittleFS patch failed after HTTP copy label=%s offset=0x%08X size=0x%08X: %s\n",
                target.label,
                target.offset,
                target.size,
                patchError.c_str()
            );
            ok = false;
        }
    }
    if (!ok && errorOut) {
        if (launcherUpdateLastError() != LAUNCHER_UPDATE_ERROR_OK) {
            *errorOut = launcherUpdateLastErrorName();
        } else if (!httpOk && response.transport_error != 0) {
            *errorOut = String("HTTP transport error ") + response.transport_error;
        } else if (!httpOk && response.status != 0) {
            *errorOut = response.status >= 300 && response.status < 400
                            ? String("Redirect ") + response.status + " not followed"
                            : String("HTTP status ") + response.status;
        } else if (!complete) {
            *errorOut = String("Download incomplete (") + update.written + "/" + imageSize + ")";
        } else {
            *errorOut = "Unknown failure";
        }
    }
    resumeInputHandlerTask();
    return ok;
}

bool installFirmwareDynamic(
    const String &fileAddr, const String &file, uint32_t appSize, uint32_t appPartitionSize,
    uint32_t appOffset, bool nb, std::vector<LauncherInstallDataPartition> &dataPartitions,
    const String &installedName
) {
    String error;
    LauncherPartitionTable table;
    if (!launcherPartitionReadCurrent(table, &error)) {
        displayError(error.length() ? error : "Partition read failed");
        return false;
    }

    size_t updateSize = appSize;
    String hwid = String(launcherWifiMac().c_str());
    if (updateSize == 0) {
        size_t remoteSize = 0;
        if (!getRemoteFileSize(fileAddr, remoteSize, hwid.c_str())) {
            displayError("Size failed");
            return false;
        }
        if (nb) {
            updateSize = remoteSize;
        } else {
            if (appOffset >= remoteSize) {
                displayError("Bad app offset");
                return false;
            }
            updateSize = remoteSize - appOffset;
        }
    }
    if (updateSize == 0) {
        displayError("Invalid app size");
        return false;
    }
    if (appPartitionSize == 0 || appPartitionSize < updateSize) appPartitionSize = updateSize;

    String appLabel = launcherInstallNextAppLabel(table, file, installedName);
    LauncherPartitionEntry appEntry;

    if (!launcherSelectInstallLayout(table, appPartitionSize, appLabel, dataPartitions, appEntry, error)) {
        displayError(error.length() ? error : "No install space");
        return false;
    }

    pauseInputHandlerTask();
    bool success = false;
    prog_handler = 0;
    progressHandler(0, updateSize);
    {
        String appError;
        if (!flashRawRangeFromHttp(
                fileAddr, nb ? 0 : appOffset, updateSize, appEntry, true, hwid.c_str(), &appError
            )) {
            displayError(String("APP: ") + appError);
            goto DONE;
        }
    }

    for (const auto &dp : dataPartitions) {
        if (!dp.hasEntry || dp.copySize == 0) continue;
        const char *typeStr = dp.subtype == 0x81 ? "FAT" : dp.subtype == 0x83 ? "LittleFS" : "SPIFFS";
        displayRedStripe(String("Installing ") + typeStr);
        prog_handler = 1;
        const uint32_t copySize = dp.copySize > dp.entry.size ? dp.entry.size : dp.copySize;
        progressHandler(0, copySize);
        // Data that ships as a separate file is fetched from its own URL at its own
        // source offset; embedded data keeps using the app image URL (fileAddr).
        const String &partAddr = dp.sourceUrl.isEmpty() ? fileAddr : dp.sourceUrl;
        String dpError;
        if (!flashRawRangeFromHttp(
                partAddr, dp.sourceOffset, copySize, dp.entry, false, hwid.c_str(), &dpError
            )) {
            displayError(String(typeStr) + ": " + dpError);
            goto DONE;
        }
    }

    displayRedStripe("Writing table");
    if (!launcherPartitionWriteGeneratedTable(table, &error)) {
        displayError(error.length() ? error : "Table failed");
        goto DONE;
    }

    displayRedStripe("Setting boot");
    if (!launcherPartitionSetOtaBoot(table, appEntry.subtype, &error)) {
        displayError(error.length() ? error : "Boot failed");
        goto DONE;
    }

    {
        std::vector<String> fatLabels;
        String registeredSpiffsLabel;
        for (const auto &dp : dataPartitions) {
            if (!dp.hasEntry) continue;
            if (dp.subtype == 0x81) fatLabels.push_back(dp.label);
            else registeredSpiffsLabel = dp.label;
        }
        launcherSaveInstalledAppMetadata(
            table, appEntry, file, installedName, fatLabels, registeredSpiffsLabel
        );
        // OTA (http) installs are not registered in backupData.json and are not
        // backed up: the source is a URL, not an SD file, so there is nothing to
        // tie a backup to when reinstalling.
    }

    saveIntoNVS();
    success = true;

DONE:
    resumeInputHandlerTask();
    if (success) {
        displayRedStripe("Restarting");
        launcherDelayMs(500);

        return releaseHeapObjectsAndReboot();
    }
    return success;
}

bool getInfo(const String &serverUrl, JsonDocument &_doc, JsonDocument *filter = nullptr) {
    if (!launcherWifiIsConnected()) {
        displayError("WiFi not connected");
        return false;
    }

    pauseInputHandlerTask();
    resetTftDisplay();
    tft->setTextSize(_fm);
    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
    tft->drawCentreString("Getting info from", tftWidth / 2, tftHeight / 3, 1);
    tft->drawCentreString("LauncherHub", tftWidth / 2, tftHeight / 3 + _fm * 9, 1);
    tft->display(false);
    tft->setCursor(18, tftHeight / 3 + _fm * 9 * 2);
    const uint8_t maxAttempts = 5;
    for (uint8_t attempt = 0; attempt < maxAttempts; ++attempt) {
        String payload;
        LauncherHttpResponse resp;
        launcherConsolePrintf("getInfo: GET attempt %u url_len=%u\n", attempt + 1, serverUrl.length());
        if (launcherHttpGetToString(serverUrl.c_str(), payload, 65536, &resp)) {
            launcherConsolePrintf("getInfo: GET ok status=%d bytes=%u\n", resp.status, payload.length());
            _doc.clear();
            RAM_LOG("getInfo-before-parse");
            DeserializationError error =
                filter ? deserializeJson(_doc, payload, DeserializationOption::Filter(*filter))
                       : deserializeJson(_doc, payload);
            if (error) {
                displayError(String("JSON Parse Failed: ") + error.c_str());
                _doc.clear();
                resumeInputHandlerTask();
                return false;
            }
            RAM_LOG("getInfo-after-parse");
            resumeInputHandlerTask();
            return true;
        }

        // Report why this attempt failed: a transport error (network/TLS/timeout)
        // surfaces as a negative esp_err_t in transport_error; otherwise the HTTP
        // status (e.g. 404, 500) explains the failure.
        String reason;
        if (resp.status >= 200 && resp.status < 600 && resp.status != 0) {
            reason = String("HTTP ") + resp.status;
        } else {
            reason = String("Net err ") + resp.transport_error;
        }
        launcherConsolePrintf(
            "getInfo: GET failed attempt=%u status=%d transport=%d\n",
            attempt + 1,
            resp.status,
            resp.transport_error
        );
        displayRedStripe(String("GET failed (") + (attempt + 1) + "/" + maxAttempts + "): " + reason);

        // The connection may have dropped mid-flow; abort early instead of burning
        // the remaining attempts (each can block up to the HTTP timeout).
        if (!launcherWifiIsConnected()) {
            displayError("WiFi lost during fetch");
            resumeInputHandlerTask();
            return false;
        }

        tftprint(".", 10);
        vTaskDelay(pdTICKS_TO_MS(500));
    }

    displayError("Server unreachable");
    resumeInputHandlerTask();
    return false;
}

/***************************************************************************************
** Function name: GetJsonFromLauncherHub
** Description:   Gets JSON from github server
***************************************************************************************/
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

String normalizeExtraQuery(String extra) {
    extra.trim();
    while (extra.startsWith("/") || extra.startsWith("\\")) { extra.remove(0, 1); }
    if (extra.length() == 0) return extra;
    if (!extra.startsWith("&") && !extra.startsWith("?")) extra = "&" + extra;
    return extra;
}

bool GetJsonFromLauncherHub(uint8_t page, const String &order, bool star, const String &query) {
    String q = "&order_by=" + order;
    q += page > 1 ? "&page=" + String(page) : "";
    q += query.length() > 0 ? "&q=" + encodeQueryValue(query) : "";
    q += star ? "&star=1" : "";
#ifdef OTA_EXTRA
    q += normalizeExtraQuery(String(OTA_EXTRA));
#endif
    String serverUrl = "https://api.launcherhub.net/firmwares?category=" + ota_tag + q;

    JsonDocument filter;
    buildFirmwareListFilter(filter);
    if (getInfo(serverUrl, doc, &filter)) {
        total_firmware = doc["total"].as<int>();
        num_pages = doc["total"].as<int>() / doc["page_size"].as<int>();
        current_page = page;
        RAM_LOG("firmwareList-doc-resident");
        return true;
    }
    displayError("Firmware list fetch Failed");
    return false;
}
JsonDocument getVersionInfo(const String &fid) {
    JsonDocument versions(launcherJsonAllocator());
    String serverUrl = "https://api.launcherhub.net/firmwares?fid=" + fid;
    if (!getInfo(serverUrl, versions)) displayError("Version fetch Failed");
    return versions;
}

// ArduinoJson's ::String converter falls back to serializeJson() (yielding the
// literal text "null") when the variant isn't a JSON string, so a plain .as<String>()
// on an absent/optional manifest field (e.g. "data": null) does NOT come back empty.
// Use this wherever a field may legitimately be null to get a real empty String instead.
static String jsonOptString(JsonVariantConst v) {
    return v.is<const char *>() ? String(v.as<const char *>()) : String();
}

// Resolves the HTTP URL a data partition's payload should be fetched from during an
// OTA install. Returns empty when the payload is embedded in the app image (manifest
// "source" is "firmware" or absent); otherwise the direct URL of the matching
// install.sources entry (e.g. a standalone SPIFFS/LittleFS image).
static String buildSourceUrl(const String &fid, const String &sourceUrl, bool useProxy);

static String resolveDataPartitionSource(JsonObject part, JsonObject sources) {
    String src = jsonOptString(part["source"]);
    if (src.isEmpty() || src == "firmware" || sources.isNull()) return String();
    String url = jsonOptString(sources[src]);
    if (url.isEmpty()) return String();
    return buildSourceUrl(String(), url, false);
}

void installFirmwareFromManifest(const String &fid, const String &version, String installedName) {
    JsonDocument detail(launcherJsonAllocator());
    String serverUrl =
        "https://api.launcherhub.net/firmwares?fid=" + fid + "&version=" + encodeQueryValue(version);
    if (!getInfo(serverUrl, detail)) {
        displayError("Install info failed");
        return;
    }

    JsonObject versionObj = detail["version"].as<JsonObject>();
    JsonObject install = versionObj["install"].as<JsonObject>();
    JsonObject app = install["app"].as<JsonObject>();
    JsonArray partitions = install["partitions"].as<JsonArray>();
    JsonObject sources = install["sources"].as<JsonObject>();
    String file = versionObj["file"].as<String>();
    if (file.isEmpty() || app.isNull()) {
        displayError("Bad install info");
        return;
    }

    uint32_t appOffset = app["source_offset"] | 0;
    uint32_t appCopySize = app["image_size"] | 0;
    uint32_t appPartitionSize = appCopySize;
    bool nb = appOffset == 0;

    std::vector<LauncherInstallDataPartition> dataPartitions;

    for (JsonObject part : partitions) {
        String type = part["type"].as<String>();
        String subtype = part["subtype"].as<String>();
        if (type == "app" && subtype == "ota") {
            appOffset = part["source_offset"] | appOffset;
            appCopySize = part["copy_size"] | appCopySize;
            appPartitionSize = appCopySize;
            nb = appOffset == 0;
        } else if (type == "data" && (subtype == "spiffs" || subtype == "littlefs")) {
            LauncherInstallDataPartition dp;
            dp.subtype = (subtype == "littlefs") ? 0x83 : 0x82;
            uint32_t declaredSize = part["size"] | 0;
            dp.sourceOffset = part["source_offset"] | 0;
            dp.copySize = part["copy_size"] | 0;
            dp.sourceUrl = resolveDataPartitionSource(part, sources);
            dp.label = part["label"].as<String>();
            if (dp.label.isEmpty()) dp.label = "spiffs";
            // accept data partitions as they are
            if (dp.label != "label" && dp.copySize > 0 && declaredSize > LAUNCHER_DEFAULT_SPIFFS_SIZE) {
                dp.partitionSize = declaredSize;
            } else if (declaredSize > LAUNCHER_DEFAULT_SPIFFS_THRESHOLD) {
                dp.partitionSize = LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE;
            } else {
                dp.partitionSize = LAUNCHER_DEFAULT_SPIFFS_SIZE;
            }
            dataPartitions.push_back(dp);
        } else if (type == "data" && subtype == "fat") {
            LauncherInstallDataPartition dp;
            dp.subtype = 0x81;
            dp.label = part["label"].as<String>();
            bool hasExistingFat = std::any_of(
                dataPartitions.begin(), dataPartitions.end(), [](const LauncherInstallDataPartition &d) {
                    return d.subtype == 0x81;
                }
            );
            uint32_t declaredSize = part["size"] | 0;
            dp.sourceOffset = part["source_offset"] | 0;
            dp.sourceUrl = resolveDataPartitionSource(part, sources);
            uint32_t requestedCopySize = part["copy_size"] | 0;
            LauncherPartitionPayloadPlan payload =
                launcherPartitionFatPayloadPlan(dp.label.c_str(), declaredSize, requestedCopySize);
            dp.partitionSize = payload.partitionSize;
            dp.copySize = payload.copySize;
            dataPartitions.push_back(dp);
        }
    }

    if (appCopySize == 0 || appPartitionSize == 0) {
        displayError("Invalid app size");
        return;
    }

    String fileAddr = buildSourceUrl(fid, file, true);
    if (!file.startsWith("https://")) file = M5_SERVER_PATH + file;
    if (fid == "") fileAddr = file;

    String manifestName = detail["name"].as<String>();
    if (!manifestName.isEmpty()) installedName = manifestName;

    if (!installFirmwareDynamic(
            fileAddr, file, appCopySize, appPartitionSize, appOffset, nb, dataPartitions, installedName
        )) {
        launcherDelayMs(2500);
    }
}
/***************************************************************************************
** Function name: downloadFirmware
** Description:   Downloads the firmware and save into the SDCard
***************************************************************************************/
void saveDownloadedFirmware(const String &folder, const String &fid, const String &version) {
    if (fid.isEmpty() || version.isEmpty()) return;
    if (!setupSdCard()) return;
    String path = folder;
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith("/")) path += "/";
    path += "downloaded.json";

    JsonDocument dwnDoc;
    File f = SDM.open(path, FILE_READ);
    if (f) {
        deserializeJson(dwnDoc, f);
        f.close();
    }
    JsonArray arr = dwnDoc.as<JsonArray>();
    if (arr.isNull()) {
        dwnDoc.clear();
        arr = dwnDoc.to<JsonArray>();
    }
    JsonObject obj;
    if (arr.size() > 0) obj = arr[0].as<JsonObject>();
    if (obj.isNull()) obj = arr.add<JsonObject>();
    obj[fid] = version;

    f = SDM.open(path, FILE_WRITE);
    if (f) {
        serializeJson(dwnDoc, f);
        f.close();
    }
}

bool checkForUpdates() {
    if (!setupSdCard()) return false;
    String path = dwn_path;
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith("/")) path += "/";
    path += "downloaded.json";

    File f = SDM.open(path, FILE_READ);
    if (!f) {
        displayError("No downloads found");
        return false;
    }
    String body;
    while (f.available()) body += (char)f.read();
    f.close();

    if (body.isEmpty() || body == "[]" || body == "[{}]") {
        displayError("No downloads found");
        return false;
    }

    displayRedStripe("Checking updates...");
    pauseInputHandlerTask();
    String serverUrl = String("https://api.launcherhub.net/updateList");
    String response;
    LauncherHttpResponse httpResp;
    bool ok = launcherHttpPost(serverUrl.c_str(), body.c_str(), body.length(), response, 65536, &httpResp);
    resumeInputHandlerTask();

    if (!ok || response.isEmpty()) {
        displayError("Update check failed");
        return false;
    }

    doc.clear();
    JsonDocument filter;
    buildFirmwareListFilter(filter);
    RAM_LOG("updateList-before-parse");
    DeserializationError err = deserializeJson(doc, response, DeserializationOption::Filter(filter));
    RAM_LOG("updateList-after-parse");
    if (err) {
        displayError("Bad server response");
        return false;
    }

    total_firmware = doc["total"] | 0;

    if (total_firmware == 0) {
        displayError("No updates found");
        return false;
    }

    // loopFirmware() reads doc["page_size"] and doc["page"] to build the list;
    // inject them so all update results fit on one page
    doc["page_size"] = (int)total_firmware;
    doc["page"] = 1;
    num_pages = 1;
    current_page = 1;
    return true;
}

/***************************************************************************************
** Function name: launcherBootloaderFlashOffset
** Description:   Flash address of the second-stage bootloader for the running chip,
**               taken straight from the IDF sdkconfig (CONFIG_BOOTLOADER_OFFSET_IN_FLASH).
***************************************************************************************/
static uint32_t launcherBootloaderFlashOffset() {
#ifdef CONFIG_BOOTLOADER_OFFSET_IN_FLASH
    return CONFIG_BOOTLOADER_OFFSET_IN_FLASH;
#elif CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
    return 0x1000;
#elif CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32C5
    return 0x2000;
#else
    return 0x0;
#endif
}

// Streams one HTTP source into the merged image, tracking the running write
// position so gaps between components can be padded with 0xFF (erased flash).
// Optionally snapshots a window of the stream (captureBuf/Start/Len) so the
// embedded partition table of a factory image can be read without seeking back.
struct MergedDownloadContext {
    File *file;
    size_t *pos;
    size_t sourceExpected;
    size_t sourceWritten;
    long progressTick;
    LauncherHttpResponse *response;
    uint8_t *captureBuf;
    size_t captureStart;
    size_t captureLen;
};

static bool mergedDownloadCb(const uint8_t *data, size_t len, void *ctx) {
    MergedDownloadContext *d = static_cast<MergedDownloadContext *>(ctx);
    if (!d || !d->file) return false;

    if (d->sourceExpected == 0 && d->response && d->response->content_length > 0) {
        d->sourceExpected = static_cast<size_t>(d->response->content_length);
        progressHandler(0, d->sourceExpected);
    }

    const size_t absStart = *d->pos; // absolute file offset this chunk lands at

    size_t totalWrote = 0;
    constexpr size_t sdWriteChunk = 512;
    while (totalWrote < len) {
        const size_t part = min(sdWriteChunk, len - totalWrote);

        size_t written = d->file->write(data + totalWrote, part);
        if (written != part) {
            d->file->flush();
            vTaskDelay(pdMS_TO_TICKS(20));
            written = d->file->write(data + totalWrote, part);
        }
        if (written != part) {
            launcherConsolePrintf(
                "Merge: SD write failed at pos 0x%06X (%u/%u bytes)\n",
                (unsigned)(*d->pos + totalWrote),
                (unsigned)written,
                (unsigned)part
            );
            return false;
        }
        totalWrote += part;
    }
    d->sourceWritten += totalWrote;
    *d->pos += totalWrote;

    // Snapshot the requested window (e.g. the partition table at 0x8000) as it
    // streams by, so callers can parse it without reopening/seeking the file.
    if (d->captureBuf && d->captureLen) {
        const size_t capEnd = d->captureStart + d->captureLen;
        const size_t s = max(absStart, d->captureStart);
        const size_t e = min(absStart + len, capEnd);
        if (s < e) memcpy(d->captureBuf + (s - d->captureStart), data + (s - absStart), e - s);
    }

    if (d->sourceExpected > 0) {
        if (d->progressTick >= 10) {
            d->file->flush();
            vTaskDelay(pdMS_TO_TICKS(2));
            progressHandler(d->sourceWritten, d->sourceExpected);
            vTaskDelay(pdMS_TO_TICKS(2));
            d->progressTick = 0;
        } else {
            d->progressTick++;
        }
    }
    return true;
}

static void waitOrDelay(bool autoAdvance, uint16_t delayMs) {
    if (autoAdvance) launcherDelayMs(delayMs);
    else
        while (!check(SelPress)) yield();
}

static bool reportDownloadOutcome(
    bool ok, const String &filePath, const String &folder, const String &fid, const String &version,
    bool autoAdvance, const char *successLog
) {
    if (!ok) {
        SDM.remove(filePath);
        displayRedStripe("Download FAILED");
        waitOrDelay(autoAdvance, 1500);
        return false;
    }

    progressHandler(100, 100);
    launcherConsolePrintln(successLog);
    saveDownloadedFirmware(folder, fid, version);
    displayRedStripe(" Downloaded ");
    waitOrDelay(autoAdvance, 1000);
    return true;
}

// Pads the merged file with 0xFF from the current write position up to `target`.
static bool padMergedFile(File &file, size_t &pos, size_t target) {
    if (pos > target) {
        launcherConsolePrintf(
            "Merge: overlap while placing source at 0x%06X (pos 0x%06X)\n", (unsigned)target, (unsigned)pos
        );
        return false; // components overlap: malformed manifest
    }
    constexpr size_t padProgressThreshold = 128 * 1024;
    constexpr size_t padProgressStep = 64 * 1024; // redraw at most once per 32KB written
    const size_t total = target - pos;
    const bool report = total > padProgressThreshold;
    if (report) {
        progressHandler(0, total); // repaints the frame, so label it afterwards
        displayRedStripe("Padding..");
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    uint8_t ff[4096];
    memset(ff, 0xFF, sizeof(ff));
    size_t done = 0, lastReported = 0;
    while (pos < target) {
        vTaskDelay(pdMS_TO_TICKS(2));
        const size_t chunk = min(sizeof(ff), target - pos);
        size_t written = file.write(ff, chunk);
        if (written != chunk) {
            file.flush();
            vTaskDelay(pdMS_TO_TICKS(20));
            written = file.write(ff, chunk);
        }
        if (written != chunk) {
            launcherConsolePrintf(
                "Merge: SD write failed while padding to 0x%06X (pos 0x%06X)\n",
                (unsigned)target,
                (unsigned)pos
            );
            return false;
        }
        pos += chunk;
        done += chunk;
        if (done - lastReported >= padProgressStep) {
            lastReported = done;
            file.flush();
            if (report) progressHandler(done, total);
            wakeUpScreen();
        }
    }
    if (report) file.flush();
    return true;
}

static String buildSourceUrl(const String &fid, const String &sourceUrl, bool useProxy) {
    String url = sourceUrl;
    if (!url.startsWith("https://")) url = M5_SERVER_PATH + url;
    if (useProxy && !fid.isEmpty()) {
        url = String("https://api.launcherhub.net/download?fid=") + fid + "&file=" + url;
    }
    return url;
}

// Downloads a single source URL into `file` at absolute flash `offset`, padding
// the preceding gap with 0xFF. Only the firmware/app source is routed through the
// LauncherHub proxy (so the backend can count the download); bootloader, partitions
// and data are fetched directly from their source URLs. When captureBuf is set, the
// window [captureStart, captureStart+captureLen) of the merged image is snapshotted.
static bool mergeSourceIntoFile(
    File &file, size_t &pos, size_t offset, const String &fid, const String &sourceUrl, bool useProxy,
    uint8_t *captureBuf = nullptr, size_t captureStart = 0, size_t captureLen = 0
) {
    if (sourceUrl.isEmpty()) return false;
    if (!padMergedFile(file, pos, offset)) return false;
    String activeUrl = buildSourceUrl(fid, sourceUrl, useProxy);

    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        LauncherHttpResponse resp;
        MergedDownloadContext ctx = {&file, &pos, 0, 0, 0, &resp, captureBuf, captureStart, captureLen};
        bool ok = launcherHttpGetStream(
            activeUrl.c_str(), mergedDownloadCb, &ctx, &resp, "HWID", launcherWifiMac().c_str()
        );
        file.flush();

        if (ok && resp.status == 200) {
            if (resp.content_length > 0 && ctx.sourceWritten != (size_t)resp.content_length) return false;
            return true;
        }

        // Nothing was written for this source yet (a redirect that failed to
        // connect never reaches the data callback), so pos/file are untouched and
        // it's safe to retry in place after falling back to /proxy. A genuine
        // mid-stream drop (some bytes already written) is not retried here, to
        // avoid writing this source's bytes twice.
        const bool unfollowedRedirect = resp.status >= 300 && resp.status < 400;
        if (attempt == 0 && ctx.sourceWritten == 0 && unfollowedRedirect &&
            activeUrl.indexOf("/download?") >= 0) {
            activeUrl.replace("/download?", "/proxy?");
            launcherConsolePrintf("Redirect not followed, falling back to /proxy for source download\n");
            continue;
        }
        return false;
    }
    return false;
}

// Scans a raw partition table blob and returns the flash offset of the first data
// partition (type byte == 0x01), or 0 if none is found. Used to locate where the
// separate data image must go inside a factory (merged) firmware.
static uint32_t firstDataPartitionOffsetFromTable(const uint8_t *table, size_t len) {
    for (size_t i = 0; i + LAUNCHER_PARTITION_ENTRY_SIZE <= len; i += LAUNCHER_PARTITION_ENTRY_SIZE) {
        const uint8_t *e = table + i;
        if (e[0] != 0xAA || e[1] != 0x50) break;  // reached end / padding
        if (e[0x02] == 0x01 && e[0x03] > 0x079) { // type == data, subtype FAT, SPIFFS or LittleFS
            return (uint32_t)e[0x04] | ((uint32_t)e[0x05] << 8) | ((uint32_t)e[0x06] << 16) |
                   ((uint32_t)e[0x07] << 24);
        }
    }
    return 0;
}

/***************************************************************************************
** Function name: downloadSplitFirmware
** Description:   Assembles a multi-source firmware into a single merged .bin on the SD
**               card, so updateFromSD can flash it like a normal image. Handles two
**               layouts:
**                 - split : separate bootloader + partitions + app [+ data], each
**                           placed at its manifest flash offset.
**                 - merged: firmware is a full factory image flashed at 0x0; a separate
**                           data image is appended at the offset declared inside the
**                           factory image's embedded partition table (0x8000).
***************************************************************************************/
static bool __attribute__((noinline)) downloadSplitFirmware(
    const String &fid, JsonObject install, const String &filePath, const String &folder,
    const String &version, bool autoAdvance
) {
    JsonObject sources = install["sources"].as<JsonObject>();
    String blUrl = jsonOptString(sources["bootloader"]);
    String partUrl = jsonOptString(sources["partitions"]);
    String fwUrl = jsonOptString(sources["firmware"]);
    String dataUrl = jsonOptString(sources["data"]);

    // A merged/factory firmware ships bootloader+partitions+app inside a single image
    // and has no separate bootloader/partitions sources.
    const bool mergedFirmware = blUrl.isEmpty() && partUrl.isEmpty();
    bool hasData = !dataUrl.isEmpty();

    // App flash offset: prefer install.app.flash_offset, then the app/ota partition
    // entry, finally the conventional 0x10000. Data offset comes from the manifest when
    // present; for merged images it is discovered from the embedded partition table.
    uint32_t appOffset = install["app"]["flash_offset"] | 0;
    uint32_t dataOffset = 0;
    for (JsonObject part : install["partitions"].as<JsonArray>()) {
        String type = part["type"].as<String>();
        String subtype = part["subtype"].as<String>();
        if (type == "app" && subtype == "ota" && appOffset == 0) {
            appOffset = part["flash_offset"] | 0;
        } else if (type == "data" && dataOffset == 0) {
            dataOffset = part["flash_offset"] | 0;
        }
    }
    if (appOffset == 0) appOffset = 0x10000;

    const uint32_t blOffset = launcherBootloaderFlashOffset();
    const uint32_t partOffset = LAUNCHER_PARTITION_TABLE_OFFSET; // 0x8000

    File file = SDM.open(filePath, FILE_WRITE);
    if (!file) {
        displayError("Fail creating file.");
        return false;
    }

    size_t pos = 0;
    bool ok = true;
    pauseInputHandlerTask();
    if (mergedFirmware) {
        // Factory image goes to 0x0. Snapshot the embedded partition table (0x8000) as
        // it streams by so we can locate the data partition afterwards.
        uint8_t ptable[LAUNCHER_PARTITION_TABLE_SIZE];
        memset(ptable, 0xFF, sizeof(ptable));
        displayRedStripe("Firmware..");
        ok = mergeSourceIntoFile(
            file, pos, 0x0, fid, fwUrl, true, hasData ? ptable : nullptr, partOffset, sizeof(ptable)
        );
        if (ok && hasData) {
            if (dataOffset == 0) dataOffset = firstDataPartitionOffsetFromTable(ptable, sizeof(ptable));
            if (dataOffset == 0 || dataOffset < pos) {
                launcherConsolePrintf(
                    "Merge: could not resolve data partition offset (0x%06X)\n", (unsigned)dataOffset
                );
                ok = false;
            } else {
                displayRedStripe("Data..");
                ok = mergeSourceIntoFile(file, pos, dataOffset, fid, dataUrl, false);
            }
        }
    } else {
        // Split image: place each component at its own flash offset.
        if (ok && !blUrl.isEmpty()) {
            displayRedStripe("Bootloader..");
            ok = mergeSourceIntoFile(file, pos, blOffset, fid, blUrl, false);
        }
        if (ok && !partUrl.isEmpty()) {
            displayRedStripe("Partitions..");
            ok = mergeSourceIntoFile(file, pos, partOffset, fid, partUrl, false);
        }
        if (ok) {
            displayRedStripe("Firmware..");
            ok = mergeSourceIntoFile(file, pos, appOffset, fid, fwUrl, true);
        }
        if (ok && hasData) {
            if (dataOffset == 0) {
                launcherConsolePrintln("Merge: split firmware missing data flash offset");
                ok = false;
            } else {
                displayRedStripe("Data..");
                ok = mergeSourceIntoFile(file, pos, dataOffset, fid, dataUrl, false);
            }
        }
    }
    file.flush();
    file.close();
    resumeInputHandlerTask();
    return reportDownloadOutcome(
        ok, filePath, folder, fid, version, autoAdvance, "Merged firmware assembled.."
    );
}

void downloadFirmware(
    const String &fid, String file_url, String fileName, String folder, const String &version,
    bool autoAdvance
) {
    displayRedStripe("Preparing..");
    RAM_LOG("downloadFirmware-start");
    String fileAddr = buildSourceUrl(fid, file_url, true);
    int tries = 0;
    fileName = replaceChars(fileName);
    prog_handler = 2;
    if (!setupSdCard()) {
        displayError("SDCard Not Found");
        return;
    }
    if (!folder.endsWith("/")) folder = folder + "/";
    if (!folder.startsWith("/")) folder = "/" + folder;
    String folder_name = folder.substring(0, folder.length() - 1);
    if (folder_name.length() > 2) {
        if (!SDM.exists(folder_name)) {
            if (!SDM.mkdir(folder_name)) {
                launcherConsolePrintf("Download: Couldn't create folder '%s'\n", folder_name.c_str());
                displayError("Can't create: '" + folder_name + "'");
                return;
            }
        }
    }
    String filePath = folder + fileName + ".bin";

    // Some firmwares need assembling before they can be flashed from SD:
    //   - split : separate bootloader + partitions + app [+ data] sources.
    //   - merged: a factory image plus a separate data (filesystem) image.
    // In both cases build one merged .bin; otherwise fall through to a plain download.
    if (!fid.isEmpty() && !version.isEmpty()) {
        JsonDocument detail(launcherJsonAllocator());
        String infoUrl =
            "https://api.launcherhub.net/firmwares?fid=" + fid + "&version=" + encodeQueryValue(version);
        if (getInfo(infoUrl, detail)) {
            JsonObject install = detail["version"]["install"].as<JsonObject>();
            JsonObject sources = install["sources"].as<JsonObject>();

            bool hasBootAndParts = !jsonOptString(sources["bootloader"]).isEmpty() &&
                                   !jsonOptString(sources["partitions"]).isEmpty();
            bool hasSeparateData = !jsonOptString(sources["data"]).isEmpty();
            if (!sources.isNull() && (hasBootAndParts || hasSeparateData)) {
                downloadSplitFirmware(fid, install, filePath, folder, version, autoAdvance);
                wakeUpScreen();
                return;
            }
        }
    }

    File file;
retry:
    file = SDM.open(filePath, FILE_WRITE);
    if (!file) {
        launcherConsolePrintf("Download: Couldn't create file %s", filePath.c_str());
        displayError("Fail creating file.");
        return;
    }
    LauncherHttpResponse response;
    prog_handler = 2;
    pauseInputHandlerTask();
    RAM_LOG("downloadFirmware-before-httpGetStream");
    FileDownloadContext download = {&file, 0, 0, 0, &response};
    String hwid = launcherWifiMac().c_str();
    bool ok =
        launcherHttpGetStream(fileAddr.c_str(), fileDownloadCb, &download, &response, "HWID", hwid.c_str());
    launcherConsolePrintf(
        "downloadFirmware: ok=%d status=%d transport_error=%d content_length=%lld downloaded=%u\n",
        (int)ok,
        response.status,
        response.transport_error,
        (long long)response.content_length,
        (unsigned)download.downloaded
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
        // A redirect the device can't follow is api.launcherhub.net's answer, not a
        // hiccup on the CDN side; use the single retry we already have to fall back
        // to /proxy instead of just repeating the doomed request.
        if (response.status >= 300 && response.status < 400 && fileAddr.indexOf("/download?") >= 0) {
            fileAddr.replace("/download?", "/proxy?");
            launcherConsolePrintf("Redirect not followed, falling back to /proxy for SD download\n");
        }
        goto retry;
    }
    ok = ok && !(response.content_length > 0 && sdSize != (size_t)response.content_length);
    reportDownloadOutcome(ok, filePath, folder, fid, version, autoAdvance, "File successfully downloaded..");
    wakeUpScreen();
}
/***************************************************************************************
** Function name: installExtFirmware
** Description:   installs External Firmware using OTA grabbing file information from url
***************************************************************************************/
bool installExtFirmware(const String &url) {
    size_t file_size;
    bool nb = 1;
    std::vector<LauncherInstallDataPartition> dataPartitions;
    uint8_t bytes[32];
    uint8_t buff[bufSize]; // on-demand range/parse buffer (was a resident global, see docs/milestone_2.md)
    if (!url.startsWith("http")) {
        displayError("Invalid link");
        return false;
    }
    displayRedStripe("Getting file info");
    LauncherHttpResponse response;
    RangeBufferContext range = {buff, bufSize, 0};
    if (!launcherHttpGetRange(url.c_str(), 32768, 416, rangeBufferCb, &range, &response) ||
        response.status != 206) {
        displayError("File not found");
        return false;
    }
    if (!parseContentRangeTotal(response.content_range, file_size)) return false;

    size_t PartitionSize = 0;
    size_t PartitionOffset = 0x10000;
    if (buff[0] == 0xAA) {
        nb = 0;
        for (int i = 0x0; i <= 0x1A0; i += 0x20) {
            memcpy(bytes, &buff[i], 32);

            if (bytes[3] == 0x00 || (bytes[3] >= 0x10 && bytes[3] <= 0x1F)) {
                if (bytes[0x0A] > 0 && PartitionSize == 0) {
                    PartitionSize = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) | bytes[0x0C];
                    PartitionOffset = (bytes[0x06] << 16) | (bytes[0x07] << 8) | bytes[0x08];
                }
            }
            if (bytes[3] == 0x81) {
                char labelBuf[17] = {0};
                memcpy(labelBuf, bytes + 12, 16);
                if (labelBuf[0] == '\0') continue;
                LauncherInstallDataPartition dp;
                dp.subtype = 0x81;
                dp.label = String(labelBuf);
                dp.sourceOffset = (bytes[0x06] << 16) | (bytes[0x07] << 8) | bytes[0x08];
                bytes[0x0C] = 0;
                uint32_t declaredSize = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) | bytes[0x0C];
                LauncherPartitionPayloadPlan payload =
                    launcherPartitionFatPayloadPlan(dp.label.c_str(), declaredSize, declaredSize);
                dp.partitionSize = payload.partitionSize;
                dp.copySize = payload.copySize;
                dataPartitions.push_back(dp);
            }
            if (bytes[3] == 0x82 || bytes[3] == 0x83) {
                char labelBuf[17] = {0};
                memcpy(labelBuf, bytes + 12, 16);
                if (labelBuf[0] == '\0') continue;
                LauncherInstallDataPartition dp;
                dp.subtype = bytes[3];
                dp.label = String(labelBuf);
                dp.sourceOffset = (bytes[0x06] << 16) | (bytes[0x07] << 8) | bytes[0x08];
                bytes[0x0C] = 0;
                dp.partitionSize = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) | bytes[0x0C];
                dp.copySize = dp.partitionSize;
                dataPartitions.push_back(dp);
            }
        }
        if (file_size < PartitionOffset + PartitionSize) PartitionSize = file_size - PartitionOffset;
        for (auto &dp : dataPartitions) {
            if (dp.subtype != 0x81 && file_size < dp.sourceOffset + dp.copySize)
                dp.copySize = file_size - dp.sourceOffset;
        }
    }
    installFirmware("", url, PartitionSize, PartitionOffset, nb, dataPartitions, "External OTA");
    return true;
}

/***************************************************************************************
** Function name: installFirmware
** Description:   installs Firmware using OTA
***************************************************************************************/
void installFirmware(
    String fid, String file, uint32_t app_size, uint32_t app_offset, bool nb,
    std::vector<LauncherInstallDataPartition> &dataPartitions, String installedName
) {
    String fileAddr = buildSourceUrl(fid, file, true);
    if (!file.startsWith("https://")) file = M5_SERVER_PATH + file;
    if (fid == "") fileAddr = file;

    {
        auto spiffsIt = std::find_if(
            dataPartitions.begin(), dataPartitions.end(), [](const LauncherInstallDataPartition &d) {
                return d.subtype != 0x81 && d.label == "spiffs";
            }
        );
        if (spiffsIt != dataPartitions.end()) {
            if (!askSpiffs) {
                spiffsIt->copySize = 0;
            } else if (spiffsIt->copySize > 0) {
                bool copySpiffs = true;
                options = {
                    {"SPIFFS No",  [&]() { copySpiffs = false; }},
                    {"SPIFFS Yes", [&]() { copySpiffs = true; } },
                };
                loopOptions(options);
                if (!copySpiffs) spiffsIt->copySize = 0;
            }
        }
    }

    tft->fillRect(7, 40, tftWidth - 14, 88, BGCOLOR); // Erase the information below the firmware name
    displayRedStripe("Connecting FW");

    if (!installFirmwareDynamic(
            fileAddr, file, app_size, app_size, app_offset, nb, dataPartitions, installedName
        )) {
        launcherDelayMs(2500);
    }
}

/***************************************************************************************
** Function name: installFAT_OTA
** Description:   install FAT partition OverTheAir
***************************************************************************************/
bool installFAT_OTA(String file, uint32_t offset, uint32_t size, const char *label) {
    prog_handler = 1; // review

    tft->fillRect(7, 40, tftWidth - 14, 88, BGCOLOR); // Erase the information below the firmware name
    displayRedStripe("Connecting FAT");

    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!partition) {
        displayError("Partition not found");
        return false;
    }

    RawHttpUpdateContext fatUpdate = {partition->address, partition->size, offset, size, 0, false, false};
    displayRedStripe("Installing FAT");
    pauseInputHandlerTask();
    bool ok = launcherHttpGetStream(file.c_str(), launcherRawUpdateHttpCb, &fatUpdate) &&
              fatUpdate.written == size && launcherRawUpdateEnd();
    resumeInputHandlerTask();
    if (ok) {
        String patchError;
        if (!launcherPatchReducedLittlefsSuperblocks(partition->address, partition->size, &patchError)) {
            launcherConsolePrintf(
                "LittleFS patch failed after FAT OTA label=%s: %s\n", label, patchError.c_str()
            );
            ok = false;
        }
    }
    vTaskDelay(pdTICKS_TO_MS(500));
    return ok;
}

#endif
