#include "webInterface.h"
#include "display.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "idf/idf_update.h"
#include "idf/idf_web_server.h"
#include "idf/idf_wifi.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "sd_functions.h"
#include "settings.h"
#include <globals.h>
#include <map>
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

struct WebParamMap {
    std::map<String, String> values;

    bool has(const char *key) const { return values.find(String(key)) != values.end(); }
    String get(const char *key) const {
        auto it = values.find(String(key));
        return it == values.end() ? String("") : it->second;
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

std::map<String, unsigned long> sessions;
bool sessionTokenLoaded = false;
String persistedSessionToken;
bool runOnce = false;

/**********************************************************************
**  Function: webUIMyNet
**  Display options to launch the WebUI
**********************************************************************/
void webUIMyNet() {
    if (!launcherWifiIsConnected()) connectWifi();
    if (launcherWifiIsConnected()) startWebUi("", 0, false);
}

/**********************************************************************
**  Function: loopOptionsWebUi
**  Display options to launch the WebUI
**********************************************************************/
void loopOptionsWebUi() {
    options = {
        {"my Network", [=]() { webUIMyNet(); }                   },
        {"AP mode",    [=]() { startWebUi("Launcher", 0, true); }},
        {"Main Menu",  [=]() { returnToMenu = true; }            },
    };

    loopOptions(options);
}

String humanReadableSize(uint64_t bytes) {
    if (bytes < 1024) return String(bytes) + " B";
    else if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + " kB";
    else if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0) + " MB";
    else return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}

String listFiles(String folder) {
    String returnText = "pa:" + folder + ":0\n";
    Serial.println("Listing files stored on SD");

    File root = SDM.open(folder);
    uploadFolder = folder;

    while (true) {
        bool isDir;
        String fullPath = root.getNextFileName(&isDir);
        String nameOnly = fullPath.substring(fullPath.lastIndexOf("/") + 1);
        if (fullPath == "") break;

        if (esp_get_free_heap_size() > (String("Fo:" + nameOnly + ":0\n").length()) + 1024) {
            if (isDir) {
                returnText += "Fo:" + nameOnly + ":0\n";
            } else {
                File fileForSize = SDM.open(fullPath);
                if (fileForSize) {
                    returnText += "Fi:" + nameOnly + ":" + humanReadableSize(fileForSize.size()) + "\n";
                    fileForSize.close();
                }
            }
        } else break;
        esp_task_wdt_reset();
    }
    root.close();
    return returnText;
}

void ensurePersistedSessionLoaded() {
    if (sessionTokenLoaded) return;
    sessionTokenLoaded = true;
    persistedSessionToken = loadSessionToken();
    if (!persistedSessionToken.isEmpty()) sessions[persistedSessionToken] = millis();
}

String generateToken(int length = 24) {
    String token = "";
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < length; i++) token += charset[random(0, sizeof(charset) - 1)];
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
        if (eq >= 0) params.values[urlDecode(pair.substring(0, eq))] = urlDecode(pair.substring(eq + 1));
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
    while (remaining > 0) {
        int readLen = httpd_req_recv(req, reinterpret_cast<char *>(buff), remaining > bufSize ? bufSize : remaining);
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
    if (boundary.startsWith("\"") && boundary.endsWith("\"")) boundary = boundary.substring(1, boundary.length() - 1);
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
        if (!name.isEmpty() && filename.isEmpty()) params.values[name] = body.substring(dataStart, next);
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
    httpd_resp_set_status(req, status == 200 ? "200 OK" : status == 400 ? "400 Bad Request" : status == 401 ? "401 Unauthorized" : status == 404 ? "404 Not Found" : "500 Internal Server Error");
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
    httpd_req_t *req, const char *contentType, bool gzip, const uint8_t *originalFile, uint32_t originalFileSize
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
        auto it = sessions.find(token);
        if (it != sessions.end()) {
            it->second = millis();
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
    Serial.print("Verifying folder: ");
    Serial.println(path);

    while (startIndex < path.length()) {
        int endIndex = path.indexOf("/", startIndex);
        if (endIndex == -1) endIndex = path.length();

        currentPath += path.substring(startIndex, endIndex);
        if (currentPath.length() > 0 && !SDM.exists(currentPath)) {
            SDM.mkdir(currentPath);
            Serial.print("Creating folder: ");
            Serial.println(currentPath);
        }

        if (endIndex < path.length()) currentPath += "/";
        startIndex = endIndex + 1;
    }
}

int findBytes(const std::vector<uint8_t> &data, const String &needle) {
    if (needle.isEmpty() || data.size() < static_cast<size_t>(needle.length())) return -1;
    for (size_t i = 0; i <= data.size() - needle.length(); ++i) {
        bool match = true;
        for (int j = 0; j < needle.length(); ++j) {
            if (data[i + j] != static_cast<uint8_t>(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return -1;
}

bool writeUploadData(File &file, const uint8_t *data, size_t len, size_t written) {
    if (!update) return file.write(data, len) == len;
    if (launcherUpdateWrite(data, len) != len) {
        displayRedStripe("FAIL 170");
        return false;
    }
    progressHandler(written + len, file_size);
    return true;
}

bool beginUploadTarget(File &file, const String &filename) {
    if (uploadFolder == "/") uploadFolder = "";
    if (!update) {
        Serial.println("File: " + uploadFolder + "/" + filename);
        String fullPath = uploadFolder + "/" + filename;
        String dirPath = fullPath.substring(0, fullPath.lastIndexOf("/"));
        if (dirPath.length() > 0) createDirRecursive(dirPath);
        file = SDM.open(fullPath, "w");
        return static_cast<bool>(file);
    }

    LauncherUpdateTarget target;
    if (launcherUpdateTargetFromCommand(command, target) && launcherUpdateBegin(target, file_size)) {
        prog_handler = target == LAUNCHER_UPDATE_APP ? 0 : 1;
        progressHandler(0, file_size);
        return true;
    }
    displayRedStripe("FAIL 160: " + String(launcherUpdateLastError()));
    delay(3000);
    return false;
}

bool finishUploadTarget(File &file) {
    if (!update) {
        file.close();
        return true;
    }
    if (!launcherUpdateEnd()) {
        displayRedStripe("Fail 181: " + String(launcherUpdateLastError()));
        delay(3000);
        return false;
    }
    lastInstalledApp = "WebUI File";
    saveIntoNVS();
    displayRedStripe("Restart your device");
    return true;
}

bool streamMultipartUpload(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return false;

    String boundary = multipartBoundary(headerValue(req, "Content-Type"));
    if (boundary.isEmpty()) {
        sendText(req, 400, "text/plain", "Missing multipart boundary");
        return false;
    }

    const String delimiter = "\r\n" + boundary;
    const size_t keep = delimiter.length() + 8;
    std::vector<uint8_t> pending;
    pending.reserve(bufSize + keep + 256);
    File file;
    bool inFile = false;
    bool finishedFile = false;
    size_t written = 0;
    size_t remaining = req->content_len;

    while (remaining > 0) {
        int readLen = httpd_req_recv(req, reinterpret_cast<char *>(buff), remaining > bufSize ? bufSize : remaining);
        if (readLen <= 0) return false;
        pending.insert(pending.end(), buff, buff + readLen);
        remaining -= readLen;

        while (!finishedFile) {
            if (!inFile) {
                int headerEnd = findBytes(pending, "\r\n\r\n");
                if (headerEnd < 0) break;
                String headers;
                headers.reserve(headerEnd);
                for (int i = 0; i < headerEnd; ++i) headers += static_cast<char>(pending[i]);
                String filename = extractDispositionValue(headers, "filename");
                pending.erase(pending.begin(), pending.begin() + headerEnd + 4);
                if (filename.isEmpty()) continue;
                if (!beginUploadTarget(file, filename)) return false;
                inFile = true;
            }

            int boundaryAt = findBytes(pending, delimiter);
            if (boundaryAt >= 0) {
                if (boundaryAt > 0 && !writeUploadData(file, pending.data(), boundaryAt, written)) return false;
                written += boundaryAt;
                pending.erase(pending.begin(), pending.begin() + boundaryAt + delimiter.length());
                finishedFile = finishUploadTarget(file);
                break;
            }

            if (pending.size() > keep) {
                size_t writeLen = pending.size() - keep;
                if (!writeUploadData(file, pending.data(), writeLen, written)) return false;
                written += writeLen;
                pending.erase(pending.begin(), pending.begin() + writeLen);
            }
            break;
        }
    }

    if (!finishedFile && inFile) {
        if (!pending.empty() && !writeUploadData(file, pending.data(), pending.size(), written)) return false;
        finishedFile = finishUploadTarget(file);
    }
    sendText(req, "text/plain", finishedFile ? "OK" : "No file");
    return finishedFile;
}

esp_err_t pingHandler(httpd_req_t *req) {
    Serial.println("WebUI /ping");
    sendText(req, "text/plain", "launcher-pong");
    return ESP_OK;
}

esp_err_t loginHandler(httpd_req_t *req) {
    WebParamMap params = readParams(req);
    if (params.has("username") && params.has("password") && params.get("username") == wui_usr &&
        params.get("password") == wui_pwd) {
        String token = generateToken();
        sessions.clear();
        sessions[token] = millis();
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
        sessions.erase(cookie.substring(start, end));
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
#ifdef PART_04MB
    sendText(req, "text/html", "");
#else
    serveWebUIFile(req, "text/html", true, logout_html, logout_html_size);
#endif
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
    else if (SDM.rename(filePath, filePath2)) sendText(req, "text/plain", filePath + " renamed to " + filePath2);
    else sendText(req, "text/plain", "Fail renaming file.");
    return ESP_OK;
}

esp_err_t otaHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;
    WebParamMap params = readParams(req);
    if (params.has("update")) {
        update = true;
        sendText(req, "text/plain", "Update");
        return ESP_OK;
    }
    if (params.has("command")) {
        command = params.get("command").toInt();
        if (params.has("size")) {
            file_size = params.get("size").toInt();
            if (file_size > 0) {
                update = true;
                runOnce = true;
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
#ifdef PART_04MB
    serveWebUIFile(req, "text/css", true, style_4mb_css, style_4mb_css_size);
#else
    serveWebUIFile(req, "text/css", true, style_css, style_css_size);
#endif
    return ESP_OK;
}

esp_err_t rootHandler(httpd_req_t *req) {
    if (req->method == HTTP_POST) {
        streamMultipartUpload(req);
        return ESP_OK;
    }
    if (checkUserWebAuth(req, true)) serveWebUIFile(req, "text/html", true, index_html, index_html_size);
    return ESP_OK;
}

esp_err_t systemInfoHandler(httpd_req_t *req) {
    char response_body[300];
    uint64_t SDTotalBytes = SDM.totalBytes();
    uint64_t SDUsedBytes = SDM.usedBytes();
    sprintf(
        response_body,
        "{\"%s\":\"%s\",\"SD\":{\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":\"%s\"}}",
        "VERSION",
        LAUNCHER,
        "free",
        humanReadableSize(SDTotalBytes - SDUsedBytes).c_str(),
        "used",
        humanReadableSize(SDUsedBytes).c_str(),
        "total",
        humanReadableSize(SDTotalBytes).c_str()
    );
    sendText(req, "application/json", response_body);
    return ESP_OK;
}

esp_err_t rebootHandler(httpd_req_t *req) {
    if (checkUserWebAuth(req)) {
        shouldReboot = true;
        sendText(req, "text/html", "Rebooting");
    }
    return ESP_OK;
}

esp_err_t listFilesHandler(httpd_req_t *req) {
    if (!checkUserWebAuth(req)) return ESP_OK;
    update = false;
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
    while (file.available()) {
        size_t readLen = file.read(buff, bufSize);
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
            Serial.printf("WebUI: ssid->%s, pwd->%s\n", ssid.c_str(), pwd.c_str());
            saveConfigs();
        } else {
            Serial.println("WebUI: failed to store new WiFi entry");
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
    registerHandler("/listfiles", HTTP_GET, listFilesHandler);
    registerHandler("/file", HTTP_GET, fileHandler);
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

#ifndef HEADLESS
    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, ALCOLOR);
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
    setTftDisplay(7, 7, ALCOLOR, FP, BGCOLOR);
    tft->drawCentreString("-= Launcher WebUI =-", tftWidth / 2, 0, 8);
#if TFT_HEIGHT < 200
    tft->drawCentreString("http://launcher.local", tftWidth / 2, 17, 1);
    setTftDisplay(7, 26, ~BGCOLOR, FP, BGCOLOR);
#else
    tft->drawCentreString("http://launcher.local", tftWidth / 2, 22, 1);
    setTftDisplay(7, 47, ~BGCOLOR, FP, BGCOLOR);
#endif
    tft->setTextSize(FM);
    tft->print("IP ");
    tftprintln(txt, 10, 1);
    tftprintln("Usr: " + String(wui_usr), 10, 1);
    tftprintln("Pwd: " + String(wui_pwd), 10, 1);
    setTftDisplay(7, tftHeight - 39, ALCOLOR, FP);
    tft->drawCentreString("press Sel to stop", tftWidth / 2, tftHeight - 15, 1);
    tft->display(false);

    while (!check(SelPress)) {
#else
    Serial.println("Access: http://launcher.local");
    Serial.print("IP ");
    Serial.println(txt);
    Serial.println("Usr: " + String(wui_usr));
    Serial.println("Pwd: " + String(wui_pwd));

    while (1) {
#endif
        if (shouldReboot) {
            FREE_TFT
            reboot();
        }
        if (updateFromSd_var) {
            updateFromSD(fileToCopy);
            updateFromSd_var = false;
            fileToCopy = "";
#ifndef HEADLESS
            displayRedStripe("Restart your Device");
#else
            Serial.println("\n\n--------------------\nRestart your Device");
#endif
        }
    }
}

void stopWebServerAndWifi() {
    launcherWebServerStop(server);
    server = nullptr;
    launcherMdnsStop();
    vTaskDelay(pdTICKS_TO_MS(100));
#if CONFIG_ESP_HOSTED_ENABLED
    launcherWifiStartSta();
#else
    launcherWifiStop();
#endif
}

void startWebUi(String ssid, int encryptation, bool mode_ap) {
    file_size = 0;
#ifndef HEADLESS
    getConfigs();
#endif
    config.httpuser = wui_usr;
    config.httppassword = wui_pwd;
    config.webserverporthttp = default_webserverporthttp;

    if (launcherWifiIsConnected() && mode_ap) launcherWifiStop();
    if (!launcherWifiIsConnected() || mode_ap) wifiConnect(ssid, encryptation, mode_ap);
    vTaskDelay(pdMS_TO_TICKS(250));

    Serial.println("Configuring Webserver ...");
    server = launcherWebServerStart(config.webserverporthttp);
    if (!server) {
        Serial.println("Failed to start Webserver");
        return;
    }
    configureWebServer();
    vTaskDelay(pdTICKS_TO_MS(500));

    startWebUiLoopCommon(mode_ap);
    stopWebServerAndWifi();
#ifndef HEADLESS
    tft->fillScreen(BGCOLOR);
#endif
}
