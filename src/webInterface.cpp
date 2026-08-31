#include "webInterface.h"
#include "app_registry.h"
#include "backup_manager.h"
#include "ble_bonds.h"
#include "display.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "idf/idf_update.h"
#include "idf/idf_web_server.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "install_shared.h"
#include "littlefs_patch.h"
#include "mykeyboard.h"
#include "nvs.h"
#include "nvs_helpers.h"
#include "onlineLauncher.h"
#include "partition_install_layout.h"
#include "partition_table_model.h"
#include "ram_profile.h"
#include "sd_functions.h"
#include "settings.h"
#include "utils.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <esp_partition.h>
#include <globals.h>
#include <memory>
#include <vector>

#include <SD.h>
#if !defined(SDM_SD)
#include <SD_MMC.h>
#endif

struct Config {
    String httpuser;
    String httppassword;
    int webserverporthttp;
};

struct WebParamEntry {
    String key;
    String value;
};

struct SessionEntry {
    String token;
    unsigned long lastSeen = 0;
};

struct WebParamMap {
    std::vector<WebParamEntry> values;

    bool has(const char *key) const {
        for (const WebParamEntry &entry : values) {
            if (entry.key == key) return true;
        }
        return false;
    }
    String get(const char *key) const {
        for (const WebParamEntry &entry : values) {
            if (entry.key == key) return entry.value;
        }
        return "";
    }
    void set(const String &key, const String &value) {
        for (WebParamEntry &entry : values) {
            if (entry.key == key) {
                entry.value = value;
                return;
            }
        }
        values.push_back({key, value});
    }
};

int command = 0;
bool updateFromSd_var = false;

const int default_webserverporthttp = 80;

Config config;
httpd_handle_t server = nullptr;
const char *host = "launcher";
bool shouldReboot = false;
String uploadFolder = "";

std::vector<SessionEntry> sessions;
bool sessionTokenLoaded = false;
String persistedSessionToken;

struct WebInstallStage {
    bool appImage = false;
    uint8_t subtype = 0xFF;
    uint32_t sourceOffset = 0;
    uint32_t copySize = 0;
    LauncherPartitionEntry entry;
    uint32_t written = 0;
    bool started = false;
};

struct WebInstallContext {
    bool active = false;
    String sourceName;
    LauncherPartitionTable table;
    LauncherPartitionEntry appEntry;
    std::vector<WebInstallStage> stages;
    size_t currentStage = 0;
    uint32_t sourcePos = 0;
    uint32_t totalCopySize = 0;
    uint32_t totalWritten = 0;
};

WebInstallContext webInstallCtx;

// Bigger recv/SD granularity than the generic 1 kB bufSize: four times fewer FAT
// transactions and recv() calls per file, which is what makes a long batch survive.
constexpr size_t kUploadChunkSize = 4096;
constexpr size_t kUploadChunkFallback = 1024;
constexpr size_t kMaxFieldValueLen = 512;

// The httpd task serves one request at a time, so every handler can share a single
// scratch buffer. Keeping it alive for the lifetime of the server removes the
// malloc/free churn (one 1 kB block per request, interleaved with String allocations)
// that fragmented the heap and made later uploads in a batch fail.
uint8_t *webScratchBuffer = nullptr;
size_t webScratchSize = 0;

uint8_t *acquireWebScratch(size_t size) {
    if (webScratchBuffer && webScratchSize >= size) return webScratchBuffer;
    free(webScratchBuffer);
    webScratchBuffer = static_cast<uint8_t *>(malloc(size));
    webScratchSize = webScratchBuffer ? size : 0;
    return webScratchBuffer;
}

void releaseWebScratch() {
    free(webScratchBuffer);
    webScratchBuffer = nullptr;
    webScratchSize = 0;
}

void clearWebInstallContext() { webInstallCtx = WebInstallContext(); }

bool failWebInstall(const String &message, bool clearContext = false) {
    displayError(message);
    if (clearContext) clearWebInstallContext();
    return false;
}

int findSessionIndex(const String &token) {
    for (size_t i = 0; i < sessions.size(); ++i) {
        if (sessions[i].token == token) return static_cast<int>(i);
    }
    return -1;
}

void setSessionToken(const String &token, unsigned long lastSeen) {
    int index = findSessionIndex(token);
    if (index >= 0) {
        sessions[index].lastSeen = lastSeen;
        return;
    }
    sessions.push_back({token, lastSeen});
}

void clearSessions() { sessions.clear(); }

void removeSessionToken(const String &token) {
    int index = findSessionIndex(token);
    if (index < 0) return;
    sessions.erase(sessions.begin() + index);
}

void addWebInstallStage(const WebInstallStage &stage) {
    size_t insertAt = webInstallCtx.stages.size();
    while (insertAt > 0 && webInstallCtx.stages[insertAt - 1].sourceOffset > stage.sourceOffset) {
        --insertAt;
    }
    webInstallCtx.stages.insert(webInstallCtx.stages.begin() + insertAt, stage);
    webInstallCtx.totalCopySize += stage.copySize;
}

bool prepareWebDataPartition(
    LauncherPartitionTable &table, uint8_t subtype, const String &label, uint32_t declaredSize,
    uint32_t copySize, LauncherPartitionEntry &entry, String &error
) {
    if (subtype == 0x81) {
        LauncherPartitionPayloadPlan payload =
            launcherPartitionFatPayloadPlan(label.c_str(), declaredSize, copySize);
        return launcherPartitionFindOrCreateData(
            table, subtype, label.c_str(), payload.partitionSize, entry, error
        );
    }
    LauncherPartitionEntry *existing = launcherPartitionFindByLabel(table, label.c_str());
    // Use the full declared size for partitions labeled different from "spiffs"
    bool useRemaining;
    uint32_t requestedSize;
    if (label != "spiffs" && label.length() > 0 && declaredSize > LAUNCHER_DEFAULT_SPIFFS_SIZE) {
        useRemaining = false;
        requestedSize = declaredSize;
    } else if (declaredSize > LAUNCHER_DEFAULT_SPIFFS_THRESHOLD) {
        useRemaining = true;
        requestedSize = LAUNCHER_DEFAULT_SPIFFS_SIZE;
    } else if (declaredSize > LAUNCHER_DEFAULT_SPIFFS_SIZE) {
        useRemaining = false;
        requestedSize = declaredSize;
    } else if (declaredSize > 0) {
        useRemaining = false;
        requestedSize = declaredSize;
    } else {
        useRemaining = false;
        requestedSize = copySize;
    }

    if (existing) {
        if (!existing->isData() || existing->subtype != subtype) {
            error = String("Partition ") + label + " is incompatible";
            return false;
        }
        if (useRemaining) {
            const uint32_t oldOffset = existing->offset;
            if (!launcherPartitionRemoveEntryByOffset(table, oldOffset)) {
                error = String("Could not resize ") + label + " partition";
                return false;
            }
            return launcherPartitionCreateDataInLargestFreeRange(table, subtype, label.c_str(), entry, error);
        }
        if (existing->size < requestedSize) {
            error = String("Partition ") + label + " is too small or incompatible";
            return false;
        }
        entry = *existing;
        return true;
    }

    if (useRemaining) {
        return launcherPartitionCreateDataInLargestFreeRange(table, subtype, label.c_str(), entry, error);
    }
    return launcherPartitionFindOrCreateData(table, subtype, label.c_str(), requestedSize, entry, error);
}

bool beginWebInstallStage(WebInstallStage &stage) {
    if (launcherRawUpdateBegin(stage.entry.offset, stage.entry.size, stage.copySize, stage.appImage)) {
        stage.started = true;
        return true;
    }
    return false;
}

bool finishWebInstallStage(WebInstallStage &stage) {
    if (stage.started) {
        if (!launcherRawUpdateEnd()) return false;
        stage.started = false;
    }
    if (!stage.appImage) {
        String patchError;
        if (!launcherPatchReducedLittlefsSuperblocks(stage.entry, &patchError)) {
            launcherConsolePrintf(
                "WebUI patch failed label=%s offset=0x%08X size=0x%08X: %s\n",
                stage.entry.label,
                stage.entry.offset,
                stage.entry.size,
                patchError.c_str()
            );
            return false;
        }
    }
    return true;
}

bool finalizeWebInstall() {
    if (webInstallCtx.currentStage != webInstallCtx.stages.size() ||
        webInstallCtx.totalWritten != webInstallCtx.totalCopySize) {
        return failWebInstall("Fail 376: 3", true);
    }

    String tableError;
    if (!launcherPartitionWriteGeneratedTable(webInstallCtx.table, &tableError)) {
        return failWebInstall(tableError.length() ? tableError : "Table failed", true);
    }
    if (webInstallCtx.appEntry.offset != 0 &&
        !launcherPartitionSetOtaBoot(webInstallCtx.table, webInstallCtx.appEntry.subtype, &tableError)) {
        return failWebInstall(tableError.length() ? tableError : "Boot failed", true);
    }

    {
        std::vector<String> fatLabels;
        String spiffsLabel;
        for (const WebInstallStage &stage : webInstallCtx.stages) {
            if (stage.appImage) continue;
            if (stage.subtype == 0x81) fatLabels.push_back(String(stage.entry.label));
            else if ((stage.subtype == 0x82 || stage.subtype == 0x83) && spiffsLabel.isEmpty())
                spiffsLabel = String(stage.entry.label);
        }
        launcherSaveInstalledAppMetadata(
            webInstallCtx.table, webInstallCtx.appEntry, webInstallCtx.sourceName, "", fatLabels, spiffsLabel
        );
    }
    clearWebInstallContext();
    saveIntoNVS();
    displayRedStripe("Restart your device");
    return true;
}

bool parseWebInstallManifest(const String &manifestJson, size_t uploadSize, String &error) {
    JsonDocument manifest;
    if (deserializeJson(manifest, manifestJson)) {
        error = "Bad manifest";
        return false;
    }

    JsonArray parts = manifest["parts"].as<JsonArray>();
    if (parts.isNull() || parts.size() == 0) {
        error = "Missing parts";
        return false;
    }

    clearWebInstallContext();
    if (!launcherPartitionReadCurrent(webInstallCtx.table, &error)) return false;
    webInstallCtx.sourceName = manifest["sourceName"].as<String>();
    webInstallCtx.totalCopySize = 0;

    bool hasApp = false;
    uint32_t appOffset = 0;
    uint32_t appSize = 0;
    uint32_t appPartitionSize = 0;
    std::vector<LauncherInstallDataPartition> dataPartitions;

    for (JsonObject part : parts) {
        String kind = part["kind"].as<String>();
        uint32_t sourceOffset = part["sourceOffset"] | 0;
        uint32_t copySize = part["copySize"] | 0;
        uint32_t declaredSize = part["declaredSize"] | copySize;
        const uint8_t subtype = static_cast<uint8_t>(part["subtype"] | 0xFF);
        const bool isSpiffsLike = subtype == 0x82 || subtype == 0x83;
        // the browser reports copySize 0 for a SPIFFS/LittleFS partition it found empty
        // (formatted, no payload); still let it through so we create it at minimum size
        if (copySize == 0 && !(isSpiffsLike && declaredSize > 0)) continue;
        if (sourceOffset > uploadSize || copySize > uploadSize - sourceOffset) {
            error = "Manifest range exceeds file";
            return false;
        }

        if (kind == "app") {
            hasApp = true;
            appOffset = sourceOffset;
            appSize = copySize;
            appPartitionSize = copySize;
            continue;
        }

        String label = part["label"].as<String>();
        if (label.isEmpty()) label = launcherInstallDefaultDataLabel(subtype);
        LauncherInstallDataPartition dp;
        dp.subtype = subtype;
        dp.label = label;
        dp.sourceOffset = sourceOffset;
        dp.copySize = copySize;
        if (subtype == 0x81) {
            LauncherPartitionPayloadPlan payload =
                launcherPartitionFatPayloadPlan(label.c_str(), declaredSize, copySize);
            dp.partitionSize = payload.partitionSize;
            dp.copySize = payload.copySize;
        } else if (isSpiffsLike) {
            const bool partitionEmpty = copySize == 0;
            // accept data partitions as they are if not "spiffs", unless there's no actual
            // payload to justify the declared size
            if (partitionEmpty && declaredSize <= LAUNCHER_DEFAULT_SPIFFS_THRESHOLD) {
                dp.partitionSize = LAUNCHER_DEFAULT_SPIFFS_SIZE;
            } else if (label != "spiffs" && declaredSize > LAUNCHER_DEFAULT_SPIFFS_SIZE) {
                dp.partitionSize = declaredSize;
            } else if (declaredSize > LAUNCHER_DEFAULT_SPIFFS_THRESHOLD) {
                dp.partitionSize = LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE;
            } else {
                dp.partitionSize = LAUNCHER_DEFAULT_SPIFFS_SIZE;
            }
        } else {
            continue;
        }
        dataPartitions.push_back(dp);
    }

    if (!hasApp || appSize == 0) {
        error = "Missing app part";
        return false;
    }

    String appLabel =
        launcherInstallNextAppLabel(webInstallCtx.table, webInstallCtx.sourceName, "", "WebUI File");

    if (!launcherSelectInstallLayout(
            webInstallCtx.table, appPartitionSize, appLabel, dataPartitions, webInstallCtx.appEntry, error
        )) {
        return false;
    }
    if (!launcherPartitionValidate(webInstallCtx.table, &error)) return false;

    WebInstallStage appStage;
    appStage.appImage = true;
    appStage.subtype = 0x00;
    appStage.sourceOffset = appOffset;
    appStage.copySize = appSize;
    appStage.entry = webInstallCtx.appEntry;
    addWebInstallStage(appStage);
    for (const auto &dp : dataPartitions) {
        if (!dp.hasEntry || dp.copySize == 0) continue;
        WebInstallStage stage;
        stage.appImage = false;
        stage.subtype = dp.entry.subtype;
        stage.sourceOffset = dp.sourceOffset;
        stage.copySize = dp.copySize > dp.entry.size ? dp.entry.size : dp.copySize;
        stage.entry = dp.entry;
        addWebInstallStage(stage);
    }
    webInstallCtx.active = !webInstallCtx.stages.empty();
    return webInstallCtx.active;
}

bool prepareWebInstallContext(int commandValue, size_t uploadSize, const WebParamMap &params, String &error) {
    if (params.has("manifest")) return parseWebInstallManifest(params.get("manifest"), uploadSize, error);

    clearWebInstallContext();

    if (!params.has("dynamic")) return false;
    if (!launcherPartitionReadCurrent(webInstallCtx.table, &error)) return false;

    webInstallCtx.sourceName = params.get("sourceName");
    WebInstallStage stage;
    stage.appImage = commandValue == LAUNCHER_UPDATE_COMMAND_FLASH;
    stage.sourceOffset = 0;
    stage.copySize = static_cast<uint32_t>(uploadSize);

    if (stage.appImage) {
        String appLabel =
            launcherInstallNextAppLabel(webInstallCtx.table, webInstallCtx.sourceName, "", "WebUI File");
        if (!launcherPartitionCreateOtaApp(
                webInstallCtx.table, stage.copySize, appLabel.c_str(), &webInstallCtx.appEntry, &error
            )) {
            return false;
        }
        stage.entry = webInstallCtx.appEntry;
    } else {
        uint8_t subtype = params.has("subtype") ? static_cast<uint8_t>(params.get("subtype").toInt()) : 0x82;
        String label = params.get("label");
        if (label.isEmpty()) label = launcherInstallDefaultDataLabel(subtype);
        const uint32_t declaredSize = params.has("declaredSize")
                                          ? static_cast<uint32_t>(params.get("declaredSize").toInt())
                                          : stage.copySize;
        if (!prepareWebDataPartition(
                webInstallCtx.table, subtype, label, declaredSize, stage.copySize, stage.entry, error
            )) {
            return false;
        }
        stage.subtype = subtype;
    }

    if (!launcherPartitionValidate(webInstallCtx.table, &error)) return false;
    webInstallCtx.totalCopySize = 0;
    addWebInstallStage(stage);
    webInstallCtx.active = true;
    return true;
}

bool writeWebInstallData(const uint8_t *data, size_t len) {
    size_t consumed = 0;
    while (consumed < len && webInstallCtx.currentStage < webInstallCtx.stages.size()) {
        WebInstallStage &stage = webInstallCtx.stages[webInstallCtx.currentStage];
        const uint32_t chunkStart = webInstallCtx.sourcePos + static_cast<uint32_t>(consumed);
        const uint32_t stageStart = stage.sourceOffset + stage.written;
        const uint32_t stageEnd = stage.sourceOffset + stage.copySize;

        if (chunkStart < stageStart) {
            const size_t skip = std::min<size_t>(len - consumed, stageStart - chunkStart);
            consumed += skip;
            continue;
        }

        if (!stage.started && !beginWebInstallStage(stage)) return false;

        const size_t writeLen = std::min<size_t>(len - consumed, stageEnd - stageStart);
        if (launcherRawUpdateWrite(data + consumed, writeLen) != writeLen) return false;

        stage.written += static_cast<uint32_t>(writeLen);
        webInstallCtx.totalWritten += static_cast<uint32_t>(writeLen);
        consumed += writeLen;
        progressHandler(webInstallCtx.totalWritten, webInstallCtx.totalCopySize);

        if (stage.written == stage.copySize) {
            if (!finishWebInstallStage(stage)) return false;
            webInstallCtx.currentStage++;
        }
    }

    webInstallCtx.sourcePos += static_cast<uint32_t>(len);
    return true;
}

/**********************************************************************
**  Function: webUIMyNet
**  Display options to launch the WebUI
**********************************************************************/
void webUIMyNet() { startWebUi("", 0, false); }

/**********************************************************************
**  Function: loopOptionsWebUi
**  Display options to launch the WebUI
**********************************************************************/
void loopOptionsWebUi() {
    if (!hostedWifiAvailable) {
        displayError("ESP-Hosted unavailable: no WiFi", true);
        return;
    }
    options = {
        {"my Network", [=]() { webUIMyNet(); }                   },
        {"AP mode",    [=]() { startWebUi("Launcher", 0, true); }},
        {"Main Menu",  [=]() { returnToMenu = true; }            },
    };

    loopOptions(options);
}

String humanReadableSize(uint64_t bytes) {
    if (bytes < 1024) return String(bytes) + " B";
    if (bytes < (1024ULL * 1024ULL)) return String((bytes + 1023) / 1024) + " kB";
    if (bytes < (1024ULL * 1024ULL * 1024ULL)) return String((bytes + 1048575) / 1048576) + " MB";
    return String((bytes + 1073741823ULL) / 1073741824ULL) + " GB";
}

String listFiles(const String &folder) {
    launcherConsolePrintln("Listing files stored on SD");

    File root = SDM.open(folder);
    uploadFolder = folder;

    String returnText = "pa:" + folder + ":0\n";
    // Grow in blocks instead of reallocating on every entry: a folder holding a few
    // hundred files used to force hundreds of realloc+copy rounds and left the heap too
    // fragmented for the uploads that follow.
    size_t reserved = 2048;
    returnText.reserve(reserved);

    while (true) {
        bool isDir;
        String fullPath = root.getNextFileName(&isDir);
        if (fullPath == "") break;
        String nameOnly = fullPath.substring(fullPath.lastIndexOf("/") + 1);

        String line;
        if (isDir) {
            line = "Fo:" + nameOnly + ":0\n";
        } else {
            File fileForSize = SDM.open(fullPath);
            if (fileForSize) {
                line = "Fi:" + nameOnly + ":" + humanReadableSize(fileForSize.size()) + "\n";
                fileForSize.close();
            }
        }

        if (line.length()) {
            if (esp_get_free_heap_size() < line.length() + 4096) break;
            if (returnText.length() + line.length() >= reserved) {
                reserved += 2048;
                returnText.reserve(reserved);
            }
            returnText += line;
        }
        esp_task_wdt_reset();
    }
    root.close();
    return returnText;
}

void ensurePersistedSessionLoaded() {
    if (sessionTokenLoaded) return;
    sessionTokenLoaded = true;
    persistedSessionToken = loadSessionToken();
    if (!persistedSessionToken.isEmpty()) setSessionToken(persistedSessionToken, launcherMillis());
}

String generateToken(int length = 24) {
    String token = "";
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < length; i++) token += charset[launcherRandom(0, sizeof(charset) - 1)];
    return token;
}

String urlDecode(const String &input) {
    String output;
    output.reserve(input.length());
    for (int i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (c == '+') {
            output += ' ';
        } else if (c == '%' && i + 2 < input.length()) {
            char hex[3] = {input[i + 1], input[i + 2], 0};
            output += static_cast<char>(strtol(hex, nullptr, 16));
            i += 2;
        } else {
            output += c;
        }
    }
    return output;
}

void parseUrlEncoded(const String &body, WebParamMap &params) {
    int start = 0;
    while (start <= body.length()) {
        int amp = body.indexOf('&', start);
        if (amp < 0) amp = body.length();
        String pair = body.substring(start, amp);
        int eq = pair.indexOf('=');
        if (eq >= 0) params.set(urlDecode(pair.substring(0, eq)), urlDecode(pair.substring(eq + 1)));
        if (amp == body.length()) break;
        start = amp + 1;
    }
}

String headerValue(httpd_req_t *req, const char *name) {
    size_t len = httpd_req_get_hdr_value_len(req, name);
    if (!len) return "";
    std::vector<char> value(len + 1);
    if (httpd_req_get_hdr_value_str(req, name, value.data(), value.size()) != ESP_OK) return "";
    return String(value.data());
}

String queryValue(httpd_req_t *req, const char *key) {
    size_t len = httpd_req_get_url_query_len(req);
    if (!len) return "";
    std::vector<char> query(len + 1);
    if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) return "";
    std::vector<char> value(512);
    if (httpd_query_key_value(query.data(), key, value.data(), value.size()) != ESP_OK) return "";
    return urlDecode(String(value.data()));
}

bool receiveBody(httpd_req_t *req, String &body, size_t maxSize = 8192) {
    if (req->content_len > maxSize) return false;
    body = "";
    body.reserve(req->content_len + 1);
    size_t remaining = req->content_len;
    uint8_t *buff = acquireWebScratch(kUploadChunkSize); // shared, see acquireWebScratch
    if (!buff) return false;
    while (remaining > 0) {
        int readLen = httpd_req_recv(
            req, reinterpret_cast<char *>(buff), remaining > kUploadChunkSize ? kUploadChunkSize : remaining
        );
        if (readLen <= 0) return false;
        body.concat(reinterpret_cast<const char *>(buff), readLen);
        remaining -= readLen;
    }
    return true;
}

String multipartBoundary(const String &contentType) {
    int idx = contentType.indexOf("boundary=");
    if (idx < 0) return "";
    String boundary = contentType.substring(idx + 9);
    int semi = boundary.indexOf(';');
    if (semi >= 0) boundary = boundary.substring(0, semi);
    boundary.trim();
    if (boundary.startsWith("\"") && boundary.endsWith("\""))
        boundary = boundary.substring(1, boundary.length() - 1);
    return "--" + boundary;
}

String extractDispositionValue(const String &headers, const char *name) {
    String key = String(name) + "=\"";
    int idx = headers.indexOf(key);
    if (idx < 0) return "";
    int start = idx + key.length();
    int end = headers.indexOf('"', start);
    if (end < 0) return "";
    return headers.substring(start, end);
}

void parseMultipartFields(const String &body, const String &contentType, WebParamMap &params) {
    String boundary = multipartBoundary(contentType);
    if (boundary.isEmpty()) return;
    int pos = 0;
    while (true) {
        int partStart = body.indexOf(boundary, pos);
        if (partStart < 0) break;
        partStart += boundary.length();
        if (body.substring(partStart, partStart + 2) == "--") break;
        if (body.substring(partStart, partStart + 2) == "\r\n") partStart += 2;
        int headerEnd = body.indexOf("\r\n\r\n", partStart);
        if (headerEnd < 0) break;
        String headers = body.substring(partStart, headerEnd);
        String name = extractDispositionValue(headers, "name");
        String filename = extractDispositionValue(headers, "filename");
        int dataStart = headerEnd + 4;
        int next = body.indexOf("\r\n" + boundary, dataStart);
        if (next < 0) break;
        if (!name.isEmpty() && filename.isEmpty()) params.set(name, body.substring(dataStart, next));
        pos = next + 2;
    }
}

WebParamMap readParams(httpd_req_t *req) {
    WebParamMap params;
    String body;
    if (!receiveBody(req, body)) return params;
    String contentType = headerValue(req, "Content-Type");
    if (contentType.indexOf("multipart/form-data") >= 0) parseMultipartFields(body, contentType, params);
    else parseUrlEncoded(body, params);
    return params;
}

void sendText(httpd_req_t *req, int status, const char *type, const String &body) {
    httpd_resp_set_status(
        req,
        status == 200   ? "200 OK"
        : status == 400 ? "400 Bad Request"
        : status == 401 ? "401 Unauthorized"
        : status == 404 ? "404 Not Found"
                        : "500 Internal Server Error"
    );
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, body.c_str(), body.length());
}

void sendText(httpd_req_t *req, const char *type, const String &body) { sendText(req, 200, type, body); }

void redirectTo(httpd_req_t *req, const String &location) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location.c_str());
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, nullptr, 0);
}

void serveWebUIFile(
    httpd_req_t *req, const char *contentType, bool gzip, const uint8_t *originalFile,
    uint32_t originalFileSize
) {
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, contentType);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (gzip) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_send(req, reinterpret_cast<const char *>(originalFile), originalFileSize);
}

bool checkUserWebAuth(httpd_req_t *req, bool onFailureReturnLoginPage = false) {
    ensurePersistedSessionLoaded();

    String cookie = headerValue(req, "Cookie");
    int idx = cookie.indexOf("ESP32SESSION=");
    if (idx != -1) {
        int start = idx + 13;
        int end = cookie.indexOf(';', start);
        if (end == -1) end = cookie.length();
        String token = cookie.substring(start, end);
        int sessionIndex = findSessionIndex(token);
        if (sessionIndex >= 0) {
            sessions[sessionIndex].lastSeen = launcherMillis();
            return true;
        }
    }
    if (onFailureReturnLoginPage) serveWebUIFile(req, "text/html", true, login_html, login_html_size);
    else sendText(req, 401, "text/plain", "Unauthorized");
    return false;
}

void createDirRecursive(String path) {
    String currentPath = "";
    int startIndex = 0;
    launcherConsolePrintf("Verifying folder: %s\n", path.c_str());

    while (startIndex < path.length()) {
        int endIndex = path.indexOf("/", startIndex);
        if (endIndex == -1) endIndex = path.length();

        currentPath += path.substring(startIndex, endIndex);
        if (currentPath.length() > 0 && !SDM.exists(currentPath)) {
            SDM.mkdir(currentPath);
            launcherConsolePrintf("Creating folder: %s\n", currentPath.c_str());
        }

        if (endIndex < path.length()) currentPath += "/";
        startIndex = endIndex + 1;
    }
}

// Boyer-Moore-ish scan built on memchr/memcmp. The previous byte-by-byte compare
// walked the whole pending window for every offset and dominated the upload loop.
int findPattern(const uint8_t *hay, size_t hayLen, const uint8_t *needle, size_t needleLen) {
    if (needleLen == 0 || hayLen < needleLen) return -1;
    const uint8_t *pos = hay;
    size_t left = hayLen;
    while (left >= needleLen) {
        const uint8_t *hit = static_cast<const uint8_t *>(memchr(pos, needle[0], left - needleLen + 1));
        if (!hit) return -1;
        if (memcmp(hit, needle, needleLen) == 0) return static_cast<int>(hit - hay);
        left -= static_cast<size_t>(hit - pos) + 1;
        pos = hit + 1;
    }
    return -1;
}

bool writeUploadData(File &file, const uint8_t *data, size_t len, size_t written) {
    if (!update) {
        size_t done = 0;
        while (done < len) {
            const size_t chunk = file.write(data + done, len - done);
            if (chunk == 0) return false; // a zero-length write means the card refused the data
            done += chunk;
        }
        return true;
    }
    if (webInstallCtx.active) {
        if (!writeWebInstallData(data, len)) {
            return failWebInstall("WebUI Update Fail: " + String(launcherUpdateLastError()));
        }
        return true;
    }
    if (launcherUpdateWrite(data, len) != len) {
        displayError("WebUI Update Fail: " + String(launcherUpdateLastError()));
        return false;
    }
    progressHandler(written + len, file_size);
    return true;
}

bool beginUploadTarget(File &file, const String &filename, const String &folder, String &outPath) {
    outPath = "";

    String destFolder = folder;
    if (destFolder == "/") destFolder = "";

    String effectiveFilename = filename;
    int firstSlash = effectiveFilename.indexOf('/');
    if (firstSlash > 0) {
        String topSegment = effectiveFilename.substring(0, firstSlash);
        String destTop = destFolder.substring(destFolder.lastIndexOf('/') + 1);
        if (!destTop.isEmpty() && destTop == topSegment) {
            effectiveFilename = effectiveFilename.substring(firstSlash + 1);
        }
    }

    if (!update) {
        launcherConsolePrintf("File: %s/%s\n", destFolder.c_str(), effectiveFilename.c_str());
        String fullPath = destFolder + "/" + effectiveFilename;
        String dirPath = fullPath.substring(0, fullPath.lastIndexOf("/"));
        if (dirPath.length() > 0) createDirRecursive(dirPath);
        file = SDM.open(fullPath, "w");
        if (!file) return false;
        outPath = fullPath;
        return true;
    }

    if (webInstallCtx.active) {
        prog_handler = 0;
        progressHandler(0, webInstallCtx.totalCopySize);
        return true;
    }
    return false;
}

bool finishUploadTarget(File &file, const String &path, size_t expected) {
    if (update) {
        if (webInstallCtx.active) return finalizeWebInstall();
        return false;
    }
    file.flush();
    file.close();
    if (path.isEmpty()) return true;
    // The write path can report success while the directory entry never lands (card
    // hiccup, full volume, dropped SPI transaction). Re-stat the file so a lost upload
    // is reported instead of answering "OK" for something that is not on the card.
    File stored = SDM.open(path, FILE_READ);
    if (!stored) return false;
    const size_t actual = stored.size();
    stored.close();
    if (actual != expected) {
        launcherConsolePrintf(
            "Upload size mismatch on %s: %u != %u\n", path.c_str(), (unsigned)actual, (unsigned)expected
        );
        return false;
    }
    return true;
}

// Discards whatever is left of the request body. ESP-IDF purges unread bodies in
// 32-byte steps, which takes forever on a multi-MB upload and leaves the keep-alive
// socket unusable; draining here with the real buffer keeps the connection in sync so
// the next file of the batch is not fed the leftovers of this one.
void drainRequestBody(httpd_req_t *req, size_t remaining, uint8_t *buff, size_t buffSize) {
    while (remaining > 0) {
        const size_t want = remaining > buffSize ? buffSize : remaining;
        const int readLen = httpd_req_recv(req, reinterpret_cast<char *>(buff), want);
        if (readLen <= 0) return;
        remaining -= static_cast<size_t>(readLen);
    }
}

enum class MultipartPhase : uint8_t { Delimiter, Headers, FieldValue, FileData, Finished };

bool streamMultipartUpload(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return false;

    String boundary = multipartBoundary(headerValue(req, "Content-Type"));
    if (boundary.isEmpty()) {
        sendText(req, 400, "text/plain", "Missing multipart boundary");
        return false;
    }

    // Every part is preceded by CRLF + boundary; seeding the buffer with a CRLF below
    // makes the opening boundary match that same pattern, so the parser needs one rule.
    const String delimiterStr = "\r\n" + boundary;
    const size_t delimiterLen = delimiterStr.length();
    const uint8_t *delimiter = reinterpret_cast<const uint8_t *>(delimiterStr.c_str());
    const size_t keep = delimiterLen + 4; // room for the "\r\n" or "--" that follows it

    size_t capacity = kUploadChunkSize + keep;
    uint8_t *buff = acquireWebScratch(capacity);
    if (!buff) {
        capacity = kUploadChunkFallback + keep;
        buff = acquireWebScratch(capacity);
    }
    if (!buff) {
        sendText(req, 500, "text/plain", "Out of memory");
        return false;
    }

    buff[0] = '\r';
    buff[1] = '\n';
    size_t len = 2;
    size_t remaining = req->content_len;

    MultipartPhase phase = MultipartPhase::Delimiter;
    String fieldName;
    String fieldValue;
    String activePath;
    // The last listed folder is only a fallback: the "folder" field of this very request
    // overrides it, so a stale global can no longer redirect an upload elsewhere.
    String folder = uploadFolder;
    File file;
    bool inFile = false;
    size_t written = 0;
    int filesStored = 0;
    String error;

    while (true) {
        bool needMore = false;

        while (!needMore && phase != MultipartPhase::Finished && error.isEmpty()) {
            switch (phase) {
                case MultipartPhase::Delimiter: {
                    const int at = findPattern(buff, len, delimiter, delimiterLen);
                    if (at < 0) {
                        // Preamble/epilogue filler: drop all but a possible partial delimiter.
                        if (len > keep) {
                            memmove(buff, buff + len - keep, keep);
                            len = keep;
                        }
                        needMore = true;
                        break;
                    }
                    const size_t after = static_cast<size_t>(at) + delimiterLen;
                    if (len - after < 2) { // need the two bytes that say "next part" or "end"
                        memmove(buff, buff + at, len - at);
                        len -= static_cast<size_t>(at);
                        needMore = true;
                        break;
                    }
                    const bool lastPart = buff[after] == '-' && buff[after + 1] == '-';
                    const size_t skip = after + 2;
                    memmove(buff, buff + skip, len - skip);
                    len -= skip;
                    phase = lastPart ? MultipartPhase::Finished : MultipartPhase::Headers;
                    break;
                }

                case MultipartPhase::Headers: {
                    static const uint8_t headerEndPattern[4] = {'\r', '\n', '\r', '\n'};
                    const int at = findPattern(buff, len, headerEndPattern, 4);
                    if (at < 0) {
                        if (len >= capacity) error = "Multipart headers too large";
                        needMore = true;
                        break;
                    }
                    String headers;
                    headers.reserve(static_cast<unsigned int>(at) + 1);
                    headers.concat(reinterpret_cast<const char *>(buff), static_cast<unsigned int>(at));
                    fieldName = extractDispositionValue(headers, "name");
                    const String filename = extractDispositionValue(headers, "filename");
                    const size_t skip = static_cast<size_t>(at) + 4;
                    memmove(buff, buff + skip, len - skip);
                    len -= skip;

                    if (filename.isEmpty()) {
                        fieldValue = "";
                        phase = MultipartPhase::FieldValue;
                        break;
                    }
                    if (!beginUploadTarget(file, filename, folder, activePath)) {
                        error = "Unable to open upload target";
                        break;
                    }
                    inFile = true;
                    written = 0;
                    phase = MultipartPhase::FileData;
                    break;
                }

                case MultipartPhase::FieldValue: {
                    const int at = findPattern(buff, len, delimiter, delimiterLen);
                    const size_t take = at >= 0 ? static_cast<size_t>(at) : (len > keep ? len - keep : 0);
                    if (take > 0) {
                        if (fieldValue.length() < kMaxFieldValueLen) {
                            fieldValue.concat(
                                reinterpret_cast<const char *>(buff),
                                static_cast<unsigned int>(take > kMaxFieldValueLen ? kMaxFieldValueLen : take)
                            );
                        }
                        memmove(buff, buff + take, len - take);
                        len -= take;
                    }
                    if (at < 0) {
                        needMore = true;
                        break;
                    }
                    if (fieldName == "folder") {
                        fieldValue.trim();
                        folder = fieldValue.length() ? fieldValue : "/";
                        uploadFolder = folder;
                    }
                    phase = MultipartPhase::Delimiter;
                    break;
                }

                case MultipartPhase::FileData: {
                    const int at = findPattern(buff, len, delimiter, delimiterLen);
                    const size_t take = at >= 0 ? static_cast<size_t>(at) : (len > keep ? len - keep : 0);
                    if (take > 0) {
                        if (!writeUploadData(file, buff, take, written)) {
                            error = "Unable to write upload data";
                            break;
                        }
                        written += take;
                        memmove(buff, buff + take, len - take);
                        len -= take;
                    }
                    if (at < 0) {
                        needMore = true;
                        break;
                    }
                    if (!finishUploadTarget(file, activePath, written)) {
                        inFile = false;
                        error = "Upload was not stored on the SD card";
                        break;
                    }
                    inFile = false;
                    filesStored++;
                    phase = MultipartPhase::Delimiter;
                    break;
                }

                default: break;
            }
        }

        if (!error.isEmpty() || phase == MultipartPhase::Finished) break;
        if (remaining == 0) break; // body exhausted before the closing boundary
        if (len >= capacity) {     // nothing consumed and no room left: unparsable
            error = "Malformed multipart data";
            break;
        }
        const size_t want = capacity - len;
        const int readLen =
            httpd_req_recv(req, reinterpret_cast<char *>(buff) + len, want > remaining ? remaining : want);
        if (readLen <= 0) {
            error = "Upload receive failed";
            break;
        }
        len += static_cast<size_t>(readLen);
        remaining -= static_cast<size_t>(readLen);
    }

    if (inFile) { // truncated body: never answer OK for a half-written file
        file.close();
        if (!activePath.isEmpty()) SDM.remove(activePath);
        if (error.isEmpty()) error = "Upload truncated";
    }

    if (remaining > 0) drainRequestBody(req, remaining, buff, capacity);

    if (!error.isEmpty()) {
        launcherConsolePrintf("Upload failed: %s\n", error.c_str());
        sendText(req, 500, "text/plain", error);
        return false;
    }
    if (filesStored == 0) {
        sendText(req, 400, "text/plain", "No file");
        return false;
    }
    sendText(req, 200, "text/plain", "OK");
    return true;
}

esp_err_t pingHandler(httpd_req_t *req) {
    launcherConsolePrintln("WebUI /ping");
    sendText(req, "text/plain", "launcher-pong");
    return ESP_OK;
}

esp_err_t loginHandler(httpd_req_t *req) {
    WebParamMap params = readParams(req);
    if (params.has("username") && params.has("password") && params.get("username") == wui_usr &&
        params.get("password") == wui_pwd) {
        String token = generateToken();
        clearSessions();
        setSessionToken(token, launcherMillis());
        saveSessionToken(token);
        sessionTokenLoaded = true;
        persistedSessionToken = token;

        // Keep cookie string alive until after httpd_resp_send — httpd_resp_set_hdr
        // stores raw pointers without copying, so a temporary String would dangle.
        String cookieHeader = "ESP32SESSION=" + token + "; Path=/; HttpOnly";
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_set_hdr(req, "Set-Cookie", cookieHeader.c_str());
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    redirectTo(req, "/?failed");
    return ESP_OK;
}

esp_err_t logoutHandler(httpd_req_t *req) {
    ensurePersistedSessionLoaded();
    String cookie = headerValue(req, "Cookie");
    int idx = cookie.indexOf("ESP32SESSION=");
    if (idx != -1) {
        int start = idx + 13;
        int end = cookie.indexOf(';', start);
        if (end == -1) end = cookie.length();
        removeSessionToken(cookie.substring(start, end));
        saveSessionToken("");
        sessionTokenLoaded = true;
        persistedSessionToken = "";
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/?loggedout");
    httpd_resp_set_hdr(req, "Set-Cookie", "ESP32SESSION=0; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t loggedOutHandler(httpd_req_t *req) {
    serveWebUIFile(req, "text/html", true, logout_html, logout_html_size);
    return ESP_OK;
}

esp_err_t updateFromSdHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;
    WebParamMap params = readParams(req);
    if (params.has("fileName")) {
        fileToCopy = params.get("fileName");
        sendText(req, "text/plain", "Starting Update");
        updateFromSd_var = true;
    } else {
        sendText(req, 400, "text/plain", "Missing fileName");
    }
    return ESP_OK;
}

esp_err_t renameHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;
    WebParamMap params = readParams(req);
    if (!params.has("fileName") || !params.has("filePath")) {
        sendText(req, 400, "text/plain", "Missing fileName or filePath");
        return ESP_OK;
    }
    String fileName = params.get("fileName");
    String filePath = params.get("filePath");
    String filePath2 = filePath.substring(0, filePath.lastIndexOf('/') + 1) + fileName;
    if (!setupSdCard()) sendText(req, "text/plain", "Fail starting SD Card.");
    else if (SDM.rename(filePath, filePath2))
        sendText(req, "text/plain", filePath + " renamed to " + filePath2);
    else sendText(req, "text/plain", "Fail renaming file.");
    return ESP_OK;
}

esp_err_t otaHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;
    WebParamMap params = readParams(req);
    if (params.has("update")) {
        clearWebInstallContext();
        update = true;
        sendText(req, "text/plain", "Update");
        return ESP_OK;
    }
    if (params.has("command")) {
        command = params.get("command").toInt();
        if (params.has("size")) {
            file_size = params.get("size").toInt();
            if (file_size > 0) {
                String error;
                if (params.has("dynamic") || params.has("manifest")) {
                    if (!prepareWebInstallContext(command, file_size, params, error)) {
                        clearWebInstallContext();
                        sendText(req, 400, "text/plain", error.length() ? error : "Install prep failed");
                        return ESP_OK;
                    }
                } else {
                    clearWebInstallContext();
                }
                update = true;
                sendText(req, "text/plain", "OK");
                return ESP_OK;
            }
        }
    }
    sendText(req, 400, "text/plain", "Invalid OTA request");
    return ESP_OK;
}

esp_err_t otaFileHandler(httpd_req_t *req) {
    streamMultipartUpload(req);
    return ESP_OK;
}

esp_err_t scriptsHandler(httpd_req_t *req) {
    serveWebUIFile(req, "application/javascript", true, scripts_js, scripts_js_size);
    return ESP_OK;
}

esp_err_t styleHandler(httpd_req_t *req) {
    serveWebUIFile(req, "text/css", true, style_css, style_css_size);
    return ESP_OK;
}

esp_err_t rootHandler(httpd_req_t *req) {
    if (req->method == HTTP_POST) {
        // File-manager uploads always land on the SD card. Without this, an OTA left in
        // flight (update stays set until the next listing) would route the payload into
        // a flash partition and still answer "OK" to the browser.
        update = false;
        clearWebInstallContext();
        streamMultipartUpload(req);
        return ESP_OK;
    }
    if (checkUserWebAuth(req, true)) serveWebUIFile(req, "text/html", true, index_html, index_html_size);
    return ESP_OK;
}

esp_err_t systemInfoHandler(httpd_req_t *req) {
    uint64_t SDTotalBytes = SDM.totalBytes();
    uint64_t SDUsedBytes = SDM.usedBytes();
    uint64_t SDFreeBytes = SDTotalBytes - SDUsedBytes;

    JsonDocument doc;
    doc["VERSION"] = LAUNCHER;
    JsonObject sd = doc["SD"].to<JsonObject>();
    sd["free"] = humanReadableSize(SDFreeBytes);
    sd["used"] = humanReadableSize(SDUsedBytes);
    sd["total"] = humanReadableSize(SDTotalBytes);
    // raw byte counts for the usage bar on the WebUI; the strings above stay human-readable
    sd["freeBytes"] = static_cast<double>(SDFreeBytes);
    sd["usedBytes"] = static_cast<double>(SDUsedBytes);
    sd["totalBytes"] = static_cast<double>(SDTotalBytes);

    JsonArray apps = doc["APPS"].to<JsonArray>();
    for (const LauncherAppMetadata &app : launcherListInstalledApps()) {
        JsonObject appObj = apps.add<JsonObject>();
        appObj["label"] = app.label;
        appObj["name"] = app.name.isEmpty() ? app.label : app.name;
    }

    String json;
    serializeJson(doc, json);
    sendText(req, "application/json", json);
    return ESP_OK;
}

esp_err_t rebootHandler(httpd_req_t *req) {
    if (checkUserWebAuth(req)) {
        shouldReboot = true;
        sendText(req, "text/html", "Rebooting");
    }
    return ESP_OK;
}

// Sets the OTA boot partition for the given app label and queues a reboot into it, mirroring
// launcherBootAppByLabel()'s on-device flow but reporting failures back to the browser instead
// of showing a device dialog (this handler runs from the web request, not the main loop).
esp_err_t bootAppHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;
    String label = queryValue(req, "label");
    WebParamMap params = readParams(req);
    if (label.isEmpty() && params.has("label")) label = params.get("label");
    if (label.isEmpty()) {
        sendText(req, 400, "text/plain", "Missing label");
        return ESP_OK;
    }

    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) {
        sendText(req, 400, "text/plain", error.length() ? error : "Partition read failed");
        return ESP_OK;
    }

    const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, label.c_str());
    if (!entry || !entry->isOtaApp()) {
        sendText(req, 400, "text/plain", "App not found");
        return ESP_OK;
    }

    if (!launcherPartitionSetOtaBoot(table, entry->subtype, &error)) {
        sendText(req, 400, "text/plain", error.length() ? error : "Boot set failed");
        return ESP_OK;
    }

    launcherBleBondsSwitchTo(label.c_str());
    lastInstalledApp = launcherAppDisplayNameForLabel(label.c_str());
    saveIntoNVS();
    shouldReboot = true;
    sendText(req, "text/plain", "OK");
    return ESP_OK;
}

esp_err_t listFilesHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;
    update = false;
    clearWebInstallContext();
    String folder = queryValue(req, "folder");
    if (folder.isEmpty()) folder = "/";
    sendText(req, "text/plain", listFiles(folder));
    return ESP_OK;
}

void sendFileDownload(httpd_req_t *req, const String &fileName) {
    File file = SDM.open(fileName);
    if (!file) {
        sendText(req, 404, "text/plain", "File not found");
        return;
    }
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    String disposition = "attachment; filename=\"" + fileName.substring(fileName.lastIndexOf('/') + 1) + "\"";
    httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());
    uint8_t *buff = acquireWebScratch(kUploadChunkSize); // shared, see acquireWebScratch
    if (!buff) {
        file.close();
        sendText(req, 500, "text/plain", "Out of memory");
        return;
    }
    while (file.available()) {
        size_t readLen = file.read(buff, kUploadChunkSize);
        if (httpd_resp_send_chunk(req, reinterpret_cast<const char *>(buff), readLen) != ESP_OK) break;
    }
    httpd_resp_send_chunk(req, nullptr, 0);
    file.close();
}

esp_err_t fileHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;
    String fileName = queryValue(req, "name");
    String fileAction = queryValue(req, "action");
    if (fileName.isEmpty() || fileAction.isEmpty()) {
        sendText(req, 400, "text/plain", "ERROR: name and action params required");
        return ESP_OK;
    }

    if (!SDM.exists(fileName)) {
        if (fileAction == "create") {
            if (!SDM.mkdir(fileName)) sendText(req, "text/plain", "FAIL creating folder: " + fileName);
            else sendText(req, "text/plain", "Created new folder: " + fileName);
        } else {
            sendText(req, 400, "text/plain", "ERROR: file does not exist");
        }
        return ESP_OK;
    }

    if (fileAction == "download") sendFileDownload(req, fileName);
    else if (fileAction == "delete") {
        if (deleteFromSd(fileName)) sendText(req, "text/plain", "Deleted : " + fileName);
        else sendText(req, "text/plain", "FAIL delating: " + fileName);
    } else if (fileAction == "create") {
        if (!SDM.mkdir(fileName)) sendText(req, "text/plain", "FAIL creating existing folder: " + fileName);
        else sendText(req, "text/plain", "Created new folder: " + fileName);
    } else {
        sendText(req, 400, "text/plain", "ERROR: invalid action param supplied");
    }
    return ESP_OK;
}

esp_err_t editfileHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;
    String fileName = queryValue(req, "name");
    if (fileName.isEmpty()) {
        sendText(req, 400, "text/plain", "Missing name");
        return ESP_OK;
    }

    if (req->method == HTTP_GET) {
        File file = SDM.open(fileName);
        if (!file) {
            sendText(req, 404, "text/plain", "Not found");
            return ESP_OK;
        }
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        uint8_t *buff = acquireWebScratch(kUploadChunkSize); // shared, see acquireWebScratch
        if (!buff) {
            file.close();
            sendText(req, 500, "text/plain", "Out of memory");
            return ESP_OK;
        }
        while (file.available()) {
            size_t len = file.read(buff, kUploadChunkSize);
            httpd_resp_send_chunk(req, reinterpret_cast<const char *>(buff), len);
        }
        httpd_resp_send_chunk(req, nullptr, 0);
        file.close();
    } else {
        String body;
        if (!receiveBody(req, body, 32768)) {
            sendText(req, 400, "text/plain", "Too large");
            return ESP_OK;
        }
        File file = SDM.open(fileName, "w");
        if (!file) {
            sendText(req, "text/plain", "FAIL");
            return ESP_OK;
        }
        file.print(body);
        file.close();
        sendText(req, "text/plain", "OK");
    }
    return ESP_OK;
}

esp_err_t nvsHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;

    if (req->method == HTTP_GET) {
        JsonDocument doc;

        nvs_iterator_t it = nullptr;
        esp_err_t res = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY, &it);

        lnvs::Handle handle;
        char curNs[16] = "";

        while (res == ESP_OK && it != nullptr) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);

            bool skip = strcmp(info.namespace_name, "launcher") == 0 && strcmp(info.key, "token") == 0;
            if (!skip) {
                if (strcmp(curNs, info.namespace_name) != 0) {
                    handle.open(info.namespace_name, false);
                    strncpy(curNs, info.namespace_name, sizeof(curNs) - 1);
                }
                if (handle) {
                    if (!doc[info.namespace_name].is<JsonArray>()) doc[info.namespace_name].to<JsonArray>();
                    JsonObject field = doc[info.namespace_name].as<JsonArray>().add<JsonObject>();
                    field["k"] = info.key;
                    field["t"] = lnvs::typeName(info.type);

                    int64_t scalar = 0;
                    size_t len = 0;
                    if (lnvs::getScalar(handle.raw(), info.key, info.type, scalar)) {
                        field["v"] = scalar;
                    } else if (info.type == NVS_TYPE_STR) {
                        field["v"] = lnvs::getString(handle.raw(), info.key);
                    } else if (
                        info.type == NVS_TYPE_BLOB &&
                        nvs_get_blob(handle.raw(), info.key, nullptr, &len) == ESP_OK
                    ) {
                        // Size only, never the payload: NimBLE bonds live in blobs and
                        // carry the LTK/IRK. The size is what matters anyway -- it is
                        // what differs between firmwares and smashes the stack.
                        field["sz"] = (uint32_t)len;
                    }
                }
            }
            res = nvs_entry_next(&it);
        }
        if (it) nvs_release_iterator(it);

        String json;
        serializeJson(doc, json);
        sendText(req, "application/json", json);
    } else {
        String body;
        if (!receiveBody(req, body)) {
            sendText(req, 400, "text/plain", "Too large");
            return ESP_OK;
        }
        JsonDocument doc;
        if (deserializeJson(doc, body)) {
            sendText(req, 400, "text/plain", "Bad JSON");
            return ESP_OK;
        }

        for (JsonPair ns : doc.as<JsonObject>()) {
            const char *nsName = ns.key().c_str();
            lnvs::Handle handle(nsName, true);
            if (!handle) continue;
            for (JsonObject field : ns.value().as<JsonArray>()) {
                const char *key = field["k"];
                if (!key) continue;
                if (strcmp(nsName, "launcher") == 0 && strcmp(key, "token") == 0) continue;

                // Blobs and unknown types have no editor in the UI and are not sent
                // back, so anything that is not a scalar or a string is ignored here.
                nvs_type_t type = lnvs::typeFromName(field["t"]);
                if (type == NVS_TYPE_STR) {
                    const char *value = field["v"].as<const char *>();
                    if (value) lnvs::setString(handle.raw(), key, value);
                } else {
                    lnvs::setScalar(handle.raw(), key, type, field["v"].as<int64_t>());
                }
            }
            handle.commit();
        }
        getFromNVS();
        getWifiFromNVS();
        sendText(req, "text/plain", "OK");
    }
    return ESP_OK;
}

// Web counterpart to PMan's "Erase BLE Bonds": drops the NimBLE bond store and the
// per-app snapshots the Launcher keeps for it, without touching WiFi or settings.
esp_err_t bleBondsHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;

    String count = String((uint32_t)launcherBleBondsCount());
    if (!launcherBleBondsEraseAll()) {
        sendText(req, 500, "text/plain", "FAIL erasing BLE bonds");
        return ESP_OK;
    }
    sendText(req, "text/plain", "Erased " + count + " BLE bond record(s)");
    return ESP_OK;
}

// ── Partition Manager (PMan) ────────────────────────────────────────────────
// Web counterpart to the on-device partList()/partitioner.cpp: edits accumulate in
// webPartCtx (nothing hits flash) until an explicit "apply" action writes the table
// and reboots, mirroring the on-device dirty-flag flow.
struct WebPartitionContext {
    bool loaded = false;
    bool dirty = false;
    LauncherPartitionTable table;
};
WebPartitionContext webPartCtx;

String webPartitionTypeName(const LauncherPartitionEntry &entry) {
    if (entry.type == 0x00) return "APP";
    if (entry.type == 0x01) return "DATA";
    if (entry.type == 0x02) return "BOOT";
    if (entry.type == 0x03) return "PTBL";
    return "UNK";
}

String webPartitionSubtypeName(const LauncherPartitionEntry &entry) {
    if (entry.type == 0x00) {
        if (entry.subtype == 0x00) return "factory";
        if (entry.subtype >= 0x10 && entry.subtype <= 0x1F) return "ota_" + String(entry.subtype - 0x10);
        if (entry.subtype == 0x20) return "test";
    }
    if (entry.type == 0x01) {
        if (entry.subtype == 0x00) return "ota";
        if (entry.subtype == 0x01) return "phy";
        if (entry.subtype == 0x02) return "nvs";
        if (entry.subtype == 0x03) return "coredump";
        if (entry.subtype == 0x81) return "fat";
        if (entry.subtype == 0x82) return "spiffs";
        if (entry.subtype == 0x83) return "littlefs";
    }
    char out[5] = {0};
    snprintf(out, sizeof(out), "%02X", entry.subtype);
    return String(out);
}

const char *webDataSubtypeName(uint8_t subtype) {
    if (subtype == ESP_PARTITION_SUBTYPE_DATA_FAT) return "FAT";
    if (subtype == 0x83) return "LittleFS";
    return "SPIFFS";
}

bool isProtectedWebPartition(const LauncherPartitionEntry &entry) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->address == entry.offset) return true;
    if (entry.isFactoryOrTestApp()) return true;
    if (entry.type == 0x01 && entry.subtype <= 0x05) return true;
    return false;
}

int findWebPartitionIndex(const LauncherPartitionTable &table, uint32_t offset) {
    for (size_t i = 0; i < table.entries.size(); ++i) {
        if (table.entries[i].offset == offset) return static_cast<int>(i);
    }
    return -1;
}

bool ensurePartitionContextLoaded(String &error) {
    if (webPartCtx.loaded && webPartCtx.dirty) return true; // keep staged edits across requests
    LauncherPartitionTable table;
    if (!launcherPartitionReadCurrent(table, &error)) return false;
    webPartCtx.table = table;
    webPartCtx.dirty = false;
    webPartCtx.loaded = true;
    return true;
}

// Reverse-lookup: which installed app (if any) owns this data partition label, checking
// the OTA app registry first (fatLabels/spiffsLabel) and falling back to the backup
// manager's manual "Associate to Bin" links for partitions with no OTA app of their own.
bool findWebAppForDataLabel(
    const String &label, const std::vector<LauncherAppMetadata> &apps, String &appName, String &appNum
) {
    for (const LauncherAppMetadata &app : apps) {
        if (app.spiffsLabel == label) {
            appName = app.name;
            appNum = app.appNum;
            return true;
        }
        for (const String &fatLabel : app.fatLabels) {
            if (fatLabel == label) {
                appName = app.name;
                appNum = app.appNum;
                return true;
            }
        }
    }
    String linkedAppNum = findAppNumByPartitionLabel(label);
    if (linkedAppNum.isEmpty()) return false;
    BackupInstallInfo info = loadInstalledFromConfig(linkedAppNum);
    if (info.appName.isEmpty()) return false;
    appName = info.appName;
    appNum = linkedAppNum;
    return true;
}

String buildPartitionsJson(const LauncherPartitionTable &table, bool dirty) {
    JsonDocument doc;
    doc["flashSize"] = table.flashSize;
    doc["dirty"] = dirty;
    const esp_partition_t *running = esp_ota_get_running_partition();
    std::vector<LauncherAppMetadata> apps = launcherLoadAppRegistry();

    JsonArray entries = doc["entries"].to<JsonArray>();
    for (size_t i = 0; i < table.entries.size(); ++i) {
        const LauncherPartitionEntry &entry = table.entries[i];
        if (entry.offset < 0x10000) continue; // hide bootloader/table system region

        JsonObject o = entries.add<JsonObject>();
        o["type"] = entry.type;
        o["subtype"] = entry.subtype;
        o["typeName"] = webPartitionTypeName(entry);
        o["subtypeName"] = webPartitionSubtypeName(entry);
        o["label"] = entry.label;
        o["offset"] = entry.offset;
        o["size"] = entry.size;
        o["flags"] = entry.flags;
        bool prot = isProtectedWebPartition(entry);
        o["protected"] = prot;
        o["running"] = running && running->address == entry.offset;

        if (entry.isApp()) {
            String appName = launcherAppDisplayNameForLabel(entry.label);
            if (!appName.isEmpty()) o["appName"] = appName;
            JsonArray dataLabels = o["dataLabels"].to<JsonArray>();
            for (const String &fatLabel : launcherAppFatLabelsForLabel(entry.label)) dataLabels.add(fatLabel);
            String spiffsLabel = launcherAppSpiffsLabelForLabel(entry.label);
            if (!spiffsLabel.isEmpty()) dataLabels.add(spiffsLabel);
        } else if (entry.isData()) {
            String appName, appNum;
            if (findWebAppForDataLabel(String(entry.label), apps, appName, appNum)) {
                o["appName"] = appName;
                o["appNum"] = appNum;
            }
        }

        if (!prot) {
            uint32_t minOffset = LAUNCHER_PARTITION_TABLE_OFFSET + LAUNCHER_PARTITION_TABLE_SIZE;
            uint32_t maxOffset = table.flashSize;
            const uint32_t currentStart = entry.offset;
            const uint32_t currentEnd = entry.offset + entry.size;
            for (size_t j = 0; j < table.entries.size(); ++j) {
                if (j == i) continue;
                const LauncherPartitionEntry &other = table.entries[j];
                const uint32_t otherEnd = other.offset + other.size;
                if (otherEnd <= currentStart && otherEnd > minOffset) minOffset = otherEnd;
                if (other.offset >= currentEnd && other.offset < maxOffset) maxOffset = other.offset;
            }
            o["minOffset"] = minOffset;
            o["maxOffset"] = maxOffset;
            o["alignment"] = launcherPartitionAlignment(entry.type, entry.subtype);
        }
    }

    JsonArray freeRanges = doc["freeRanges"].to<JsonArray>();
    for (const LauncherPartitionRange &range : launcherPartitionFreeRanges(table)) {
        if (range.size == 0) continue;
        JsonObject o = freeRanges.add<JsonObject>();
        o["offset"] = range.offset;
        o["size"] = range.size;
    }

    String json;
    serializeJson(doc, json);
    return json;
}

bool webPartitionResize(WebParamMap &params, String &error) {
    if (!ensurePartitionContextLoaded(error)) return false;
    if (!params.has("offset") || !params.has("size")) {
        error = "Missing offset or size";
        return false;
    }
    uint32_t offset = strtoul(params.get("offset").c_str(), nullptr, 0);
    uint32_t newSize = strtoul(params.get("size").c_str(), nullptr, 0);
    uint32_t newOffset =
        params.has("newOffset") ? strtoul(params.get("newOffset").c_str(), nullptr, 0) : offset;

    LauncherPartitionTable &table = webPartCtx.table;
    int index = findWebPartitionIndex(table, offset);
    if (index < 0) {
        error = "Partition not found";
        return false;
    }
    if (isProtectedWebPartition(table.entries[index])) {
        error = "Protected partition";
        return false;
    }
    if (newSize == 0) {
        error = "Invalid size";
        return false;
    }

    LauncherPartitionTable edited = table;
    edited.entries[index].offset = newOffset;
    edited.entries[index].size = newSize;
    if (!launcherPartitionValidate(edited, &error)) return false;
    if (!launcherPartitionCompact(edited, &error)) return false;
    table = edited;
    webPartCtx.dirty = true;
    return true;
}

bool webPartitionCreate(WebParamMap &params, String &error) {
    if (!ensurePartitionContextLoaded(error)) return false;
    if (!params.has("type") || !params.has("subtype") || !params.has("label") || !params.has("size")) {
        error = "Missing type, subtype, label or size";
        return false;
    }
    uint8_t type = static_cast<uint8_t>(strtoul(params.get("type").c_str(), nullptr, 0));
    uint8_t subtype = static_cast<uint8_t>(strtoul(params.get("subtype").c_str(), nullptr, 0));
    String label = params.get("label");
    if (label.isEmpty()) {
        error = "Label required";
        return false;
    }
    uint32_t alignment = launcherPartitionAlignment(type, subtype);
    uint32_t size = launcherAlignUp(strtoul(params.get("size").c_str(), nullptr, 0), alignment);

    LauncherPartitionTable edited = webPartCtx.table;
    LauncherPartitionEntry created;
    created.type = type;
    created.subtype = subtype;
    created.flags = 0;
    memset(created.label, 0, sizeof(created.label));
    strncpy(created.label, label.c_str(), 15);

    if (params.has("offset")) {
        created.offset = strtoul(params.get("offset").c_str(), nullptr, 0);
    } else {
        LauncherPartitionRange range;
        if (!launcherPartitionFindFreeRange(edited, size, alignment, range, &error)) return false;
        created.offset = range.offset;
    }
    created.size = size;

    if (type == 0x00) {
        int nextSubtype = launcherPartitionNextOtaSubtype(edited);
        if (nextSubtype < 0) {
            error = "No OTA slot available";
            return false;
        }
        created.subtype = static_cast<uint8_t>(nextSubtype);
    }

    if (!launcherPartitionAdd(edited, created, &error)) return false;
    if (!launcherPartitionCompact(edited, &error)) return false;
    webPartCtx.table = edited;
    webPartCtx.dirty = true;
    return true;
}

bool webPartitionDelete(WebParamMap &params, String &error) {
    if (!ensurePartitionContextLoaded(error)) return false;
    if (!params.has("offset")) {
        error = "Missing offset";
        return false;
    }
    uint32_t offset = strtoul(params.get("offset").c_str(), nullptr, 0);
    LauncherPartitionTable &table = webPartCtx.table;
    int index = findWebPartitionIndex(table, offset);
    if (index < 0) {
        error = "Partition not found";
        return false;
    }
    if (isProtectedWebPartition(table.entries[index])) {
        error = "Protected partition";
        return false;
    }

    LauncherPartitionTable edited = table;
    edited.entries.erase(edited.entries.begin() + index);
    if (!launcherPartitionValidate(edited, &error)) return false;
    if (!launcherPartitionCompact(edited, &error)) return false;
    table = edited;
    webPartCtx.dirty = true;
    return true;
}

bool webPartitionFormat(WebParamMap &params, String &error) {
    if (webPartCtx.dirty) {
        error = "Apply or discard pending changes first";
        return false;
    }
    if (!ensurePartitionContextLoaded(error)) return false;
    if (!params.has("offset")) {
        error = "Missing offset";
        return false;
    }
    uint32_t offset = strtoul(params.get("offset").c_str(), nullptr, 0);
    int index = findWebPartitionIndex(webPartCtx.table, offset);
    if (index < 0) {
        error = "Partition not found";
        return false;
    }
    const LauncherPartitionEntry &entry = webPartCtx.table.entries[index];
    if (isProtectedWebPartition(entry) || !entry.isData()) {
        error = "Cannot format";
        return false;
    }
    if (!launcherRawPrepareDataPartition(entry.offset, entry.size)) {
        error = "Format failed";
        return false;
    }
    return true;
}

bool webPartitionApply(String &error) {
    if (!ensurePartitionContextLoaded(error)) return false;
    LauncherPartitionTable target = webPartCtx.table;
    if (!launcherPartitionCompact(target, &error)) return false;
    if (!launcherPartitionValidate(target, &error)) return false;

    LauncherPartitionTable current;
    if (!launcherPartitionReadCurrent(current, &error)) return false;
    if (!launcherPartitionMigrateMovedData(current, target, &error)) return false;
    if (!launcherPartitionWriteGeneratedTable(target, &error)) return false;

    webPartCtx.dirty = false;
    webPartCtx.loaded = false;
    shouldReboot = true;
    return true;
}

bool webPartitionDiscard(String &error) {
    LauncherPartitionTable table;
    if (!launcherPartitionReadCurrent(table, &error)) return false;
    webPartCtx.table = table;
    webPartCtx.dirty = false;
    webPartCtx.loaded = true;
    return true;
}

// Headless equivalent of dumpPartition() for data partitions that aren't linked to any
// installed app (no appNum to file the backup under in backupData.json).
String webBackupPartitionRaw(const LauncherPartitionEntry &entry) {
    if (!setupSdCard()) return "";
    if (!SDM.exists("/bkp")) SDM.mkdir("/bkp");

    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, entry.label);
    if (!partition) return "";

    String base = String("/bkp/") + entry.label;
    String output = base + ".bin";
    int suffix = 0;
    while (SDM.exists(output)) {
        suffix++;
        output = base + String(suffix) + ".bin";
    }

    File outFile = SDM.open(output, FILE_WRITE, true);
    if (!outFile) return "";

    std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[4096]);
    if (!buffer) {
        outFile.close();
        return "";
    }
    for (size_t offset = 0; offset < partition->size; offset += 4096) {
        size_t chunk = std::min<size_t>(4096, partition->size - offset);
        if (esp_partition_read(partition, offset, buffer.get(), chunk) != ESP_OK) {
            outFile.close();
            return "";
        }
        outFile.write(buffer.get(), chunk);
    }
    outFile.close();
    return output;
}

bool webPartitionBackup(WebParamMap &params, String &error, String &outPath) {
    if (!params.has("label")) {
        error = "Missing label";
        return false;
    }
    String label = params.get("label");
    String appNum = findAppNumByPartitionLabel(label);
    if (!appNum.isEmpty()) {
        LauncherPartitionTable table;
        String readError;
        String typeName = "SPIFFS";
        if (launcherPartitionReadCurrent(table, &readError)) {
            const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, label.c_str());
            if (entry) typeName = webDataSubtypeName(entry->subtype);
        }
        outPath = backupPartition(appNum, label.c_str(), typeName.c_str());
    } else {
        LauncherPartitionTable table;
        if (!launcherPartitionReadCurrent(table, &error)) return false;
        const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, label.c_str());
        if (!entry) {
            error = "Partition not found";
            return false;
        }
        outPath = webBackupPartitionRaw(*entry);
    }
    if (outPath.isEmpty()) {
        error = "Backup failed";
        return false;
    }
    return true;
}

bool webPartitionRestore(WebParamMap &params, String &error) {
    if (!params.has("label") || !params.has("path")) {
        error = "Missing label or path";
        return false;
    }
    String label = params.get("label");
    String path = params.get("path");
    if (!restorePartitionFromBackup(label.c_str(), path.c_str())) {
        error = "Restore failed";
        return false;
    }
    return true;
}

void collectWebPartitionBackups(JsonArray &arr, const String &label) {
    String appNum = findAppNumByPartitionLabel(label);
    if (!appNum.isEmpty()) {
        BackupInstallInfo info = loadInstalledFromConfig(appNum);
        for (const BackupPartitionInfo &bp : info.partitions) {
            if (bp.label != label || bp.lastBackupPath.isEmpty()) continue;
            JsonObject o = arr.add<JsonObject>();
            o["path"] = bp.lastBackupPath;
            o["type"] = bp.type;
        }
        return;
    }
    if (!setupSdCard() || !SDM.exists("/bkp")) return;
    File root = SDM.open("/bkp");
    if (!root || !root.isDirectory()) return;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        String name = String(f.name());
        bool isDir = f.isDirectory();
        f.close();
        int slash = name.lastIndexOf('/');
        String base = slash >= 0 ? name.substring(slash + 1) : name;
        if (!isDir && base.startsWith(label) && base.endsWith(".bin")) {
            JsonObject o = arr.add<JsonObject>();
            o["path"] = name.startsWith("/") ? name : "/bkp/" + name;
            o["type"] = "RAW";
        }
    }
    root.close();
}

esp_err_t partitionsHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;

    if (req->method == HTTP_GET) {
        String listParam = queryValue(req, "list");
        if (listParam == "backups") {
            JsonDocument doc;
            JsonArray arr = doc["backups"].to<JsonArray>();
            collectWebPartitionBackups(arr, queryValue(req, "label"));
            String json;
            serializeJson(doc, json);
            sendText(req, "application/json", json);
            return ESP_OK;
        }

        String error;
        if (!ensurePartitionContextLoaded(error)) {
            sendText(req, 400, "text/plain", error.length() ? error : "Partition read failed");
            return ESP_OK;
        }
        sendText(req, "application/json", buildPartitionsJson(webPartCtx.table, webPartCtx.dirty));
        return ESP_OK;
    }

    WebParamMap params = readParams(req);
    String action = params.get("action");
    String error;
    String resultPath;
    bool ok;

    if (action == "resize") ok = webPartitionResize(params, error);
    else if (action == "create") ok = webPartitionCreate(params, error);
    else if (action == "delete") ok = webPartitionDelete(params, error);
    else if (action == "format") ok = webPartitionFormat(params, error);
    else if (action == "apply") ok = webPartitionApply(error);
    else if (action == "discard") ok = webPartitionDiscard(error);
    else if (action == "backup") ok = webPartitionBackup(params, error, resultPath);
    else if (action == "restore") ok = webPartitionRestore(params, error);
    else {
        ok = false;
        error = "Unknown action";
    }

    if (!ok) {
        sendText(req, 400, "text/plain", error.length() ? error : "Failed");
        return ESP_OK;
    }
    if (action == "backup") {
        JsonDocument doc;
        doc["path"] = resultPath;
        String json;
        serializeJson(doc, json);
        sendText(req, "application/json", json);
    } else if (action == "apply") {
        sendText(req, "text/plain", "OK"); // device reboots from the main loop once the response flushes
    } else {
        sendText(req, "application/json", buildPartitionsJson(webPartCtx.table, webPartCtx.dirty));
    }
    return ESP_OK;
}

esp_err_t sdPinsHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;
    String misoStr = queryValue(req, "miso");
    String mosiStr = queryValue(req, "mosi");
    String sckStr = queryValue(req, "sck");
    String csStr = queryValue(req, "cs");
    if (misoStr.isEmpty() || mosiStr.isEmpty() || sckStr.isEmpty() || csStr.isEmpty()) return ESP_OK;
#if defined(HEADLESS)
    int miso = misoStr.toInt();
    int mosi = mosiStr.toInt();
    int sck = sckStr.toInt();
    int cs = csStr.toInt();
    if (miso > 44 || mosi > 44 || sck > 44 || cs > 44 || miso < 0 || mosi < 0 || sck < 0 || cs < 0) {
        sendText(req, "text/plain", "Pins not configured.");
        return ESP_OK;
    }
    _sck = sck;
    _miso = miso;
    _mosi = mosi;
    _cs = cs;
    saveIntoNVS();
    setupSdCard();
    sendText(req, "text/plain", "Pins configured.");
#else
    sendText(req, "text/plain", "Functionality exclusive for Headless environment (devices with no screen)");
#endif
    return ESP_OK;
}

esp_err_t wifiHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;
    String usr = queryValue(req, "usr");
    String pwdd = queryValue(req, "pwd");
    if (!usr.isEmpty() && !pwdd.isEmpty()) {
        wui_pwd = pwdd;
        wui_usr = usr;
        saveConfigs();
        config.httpuser = usr;
        config.httppassword = pwdd;
        sendText(req, "text/plain", "User: " + String(ssid) + " configured with password: " + String(pwd));
        return ESP_OK;
    }

    String ssidd = queryValue(req, "ssid");
    if (!ssidd.isEmpty() && !pwdd.isEmpty()) {
        pwd = pwdd;
        ssid = ssidd;
        if (setWifiCredential(ssid, pwd)) {
            saveConfigs();
        } else {
            launcherConsolePrintln("WebUI: failed to store new WiFi entry");
        }
    }
    sendText(req, "text/plain", "OK");
    return ESP_OK;
}

esp_err_t fallbackHandler(httpd_req_t *req) {
    redirectTo(req, "/");
    return ESP_OK;
}

void registerHandler(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *)) {
    httpd_uri_t route = {};
    route.uri = uri;
    route.method = method;
    route.handler = handler;
    route.user_ctx = nullptr;
    httpd_register_uri_handler(server, &route);
}

void configureWebServer() {
    ensurePersistedSessionLoaded();

    launcherMdnsStart(host, config.webserverporthttp);

    registerHandler("/ping", HTTP_GET, pingHandler);
    registerHandler("/login", HTTP_POST, loginHandler);
    registerHandler("/logout", HTTP_GET, logoutHandler);
    registerHandler("/logged-out", HTTP_GET, loggedOutHandler);
    registerHandler("/UPDATE", HTTP_POST, updateFromSdHandler);
    registerHandler("/rename", HTTP_POST, renameHandler);
    registerHandler("/OTA", HTTP_POST, otaHandler);
    registerHandler("/OTAFILE", HTTP_POST, otaFileHandler);
    registerHandler("/scripts.js", HTTP_GET, scriptsHandler);
    registerHandler("/style.css", HTTP_GET, styleHandler);
    registerHandler("/", HTTP_GET, rootHandler);
    registerHandler("/", HTTP_POST, rootHandler);
    registerHandler("/systeminfo", HTTP_GET, systemInfoHandler);
    registerHandler("/reboot", HTTP_GET, rebootHandler);
    registerHandler("/bootapp", HTTP_GET, bootAppHandler);
    registerHandler("/bootapp", HTTP_POST, bootAppHandler);
    registerHandler("/listfiles", HTTP_GET, listFilesHandler);
    registerHandler("/file", HTTP_GET, fileHandler);
    registerHandler("/editfile", HTTP_GET, editfileHandler);
    registerHandler("/editfile", HTTP_POST, editfileHandler);
    registerHandler("/nvs", HTTP_GET, nvsHandler);
    registerHandler("/nvs", HTTP_POST, nvsHandler);
    registerHandler("/blebonds", HTTP_POST, bleBondsHandler);
    registerHandler("/partitions", HTTP_GET, partitionsHandler);
    registerHandler("/partitions", HTTP_POST, partitionsHandler);
    registerHandler("/sdpins", HTTP_GET, sdPinsHandler);
    registerHandler("/wifi", HTTP_GET, wifiHandler);
    registerHandler("/*", HTTP_GET, fallbackHandler);
    registerHandler("/*", HTTP_POST, fallbackHandler);
}

String readLineFromFile(File myFile) {
    String line = "";
    char character;

    while (myFile.available()) {
        character = myFile.read();
        if (character == ';') break;
        line += character;
    }
    return line;
}

void startWebUiLoopCommon(bool mode_ap) {
    String txt;
    if (!mode_ap) txt = launcherWifiLocalIp().c_str();
    else txt = launcherWifiApIp().c_str();

#if !defined(HEADLESS) || defined(HEADLESS_WITH_TFT)
    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, ALCOLOR);
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
    setTftDisplay(7, 7, ALCOLOR, _fp, BGCOLOR);
    tft->drawCentreString("-= Launcher WebUI =-", tftWidth / 2, 0, 8);
    // Short panels pull the URL and the body up.
    const bool shortPanel = panelHeight() < 200;
    tft->drawCentreString("http://launcher.local", tftWidth / 2, shortPanel ? 17 : 22, 1);
    setTftDisplay(7, shortPanel ? 26 : 47, ~BGCOLOR, _fp, BGCOLOR);
    tft->setTextSize(_fm);
    tft->print("IP ");
    tftprintln(txt, 10, 1);
    tftprintln("Usr: " + String(wui_usr), 10, 1);
    tftprintln("Pwd: " + String(wui_pwd), 10, 1);
    setTftDisplay(7, tftHeight - 39, ALCOLOR, _fp);
    tft->drawCentreString("press Sel to stop", tftWidth / 2, tftHeight - 15, 1);
    tft->display(false);
#endif

    launcherConsolePrintln("Access: http://launcher.local");
    launcherConsolePrintf("IP %s\n", txt.c_str());
    launcherConsolePrintf("Usr: %s\n", wui_usr.c_str());
    launcherConsolePrintf("Pwd: %s\n", wui_pwd.c_str());

#if !defined(HEADLESS)
    while (!check(SelPress)) {
#else
    while (1) {
#endif
        if (shouldReboot) { return (void)releaseHeapObjectsAndReboot(); }
        if (updateFromSd_var) {
            updateFromSD(fileToCopy);
            updateFromSd_var = false;
            fileToCopy = "";
#ifndef HEADLESS
            displayRedStripe("Restart your Device");
#else
            launcherConsolePrintln("\n\n--------------------\nRestart your Device");
#endif
        }
    }
}

void stopWebServerAndWifi() {
    launcherWebServerStop(server);
    server = nullptr;
    releaseWebScratch();
    launcherMdnsStop();
    vTaskDelay(pdTICKS_TO_MS(100));
#if CONFIG_ESP_HOSTED_ENABLED
    launcherWifiStartSta();
#else
    launcherWifiStop();
#endif
}

void startWebUi(const String &ssid, int encryptation, bool mode_ap) {
    RAM_LOG(mode_ap ? "startWebUi-ap-start" : "startWebUi-sta-start");
    file_size = 0;
#if defined(ENABLE_ESP_AT_INTERFACE)
    if (launcherWifiActiveBackend() == LauncherWifiBackend::EspAt) {
        launcherConsolePrintln("WebUI unavailable: ESP-AT backend has no local TCP/IP stack");
        displayError("WebUI unavailable with ESP-AT WiFi");
        return;
    }
#endif
#ifndef HEADLESS
    getConfigs();
#endif
    config.httpuser = wui_usr;
    config.httppassword = wui_pwd;
    config.webserverporthttp = default_webserverporthttp;

    if (launcherWifiIsConnected() && mode_ap) launcherWifiStop();
    RAM_LOG("startWebUi-before-wifi");
    if (!ensureWifiConnected(ssid, encryptation, mode_ap)) return;
    vTaskDelay(pdMS_TO_TICKS(250));

    launcherConsolePrintln("Configuring Webserver ...");
    RAM_LOG("before-webserver-start");
    server = launcherWebServerStart(config.webserverporthttp);
    if (!server) {
        launcherConsolePrintln("Failed to start Webserver");
        return;
    }
    configureWebServer();
    RAM_LOG("after-webserver-configure");
    vTaskDelay(pdTICKS_TO_MS(500));

    startWebUiLoopCommon(mode_ap);
    stopWebServerAndWifi();
    RAM_LOG("after-webui-stop");
#ifndef HEADLESS
    tft->fillScreen(BGCOLOR);
#endif
}
