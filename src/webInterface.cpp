
#include "webInterface.h"
#include "display.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "sd_functions.h"
#include "settings.h"
#include <globals.h>
#include <map>

struct Config {
    String httpuser;
    String httppassword;   // password to access web admin
    int webserverporthttp; // http port number for web admin
};

// variables
// command = U_SPIFFS = 100
// command = U_FLASH = 0
int command = 0;
bool updateFromSd_var = false;

// WiFi as a Client
const int default_webserverporthttp = 80;

// WiFi as an Access Point
IPAddress AP_GATEWAY(172, 0, 0, 1); // Gateway

Config config; // configuration

AsyncWebServer *server; // initialise webserver
const char *host = "launcher";
bool shouldReboot = false; // schedule a reboot
String uploadFolder = "";

/**********************************************************************
**  Function: webUIMyNet
**  Display options to launch the WebUI
**********************************************************************/
void webUIMyNet() {
    if (WiFi.status() != WL_CONNECTED) connectWifi();
    if (WiFi.status() == WL_CONNECTED) startWebUi("", 0, false);
}

/**********************************************************************
**  Function: loopOptionsWebUi
**  Display options to launch the WebUI
**********************************************************************/
void loopOptionsWebUi() {
    // Definição da matriz "Options"
    options = {
        {"my Network", [=]() { webUIMyNet(); }                   },
        {"AP mode",    [=]() { startWebUi("Launcher", 0, true); }},
        {"Main Menu",  [=]() { returnToMenu = true; }            },
    };

    loopOptions(options);
    // On fail installing will run the following line
}

// Make size of files human readable
// source: https://github.com/CelliesProjects/minimalUploadAuthESP32
String humanReadableSize(uint64_t bytes) {
    if (bytes < 1024) return String(bytes) + " B";
    else if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + " kB";
    else if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0) + " MB";
    else return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}

// list all of the files, if ishtml=true, return html rather than simple text
String listFiles(String folder) {
    // log_i("Listfiles Start");
    String returnText = "pa:" + folder + ":0\n";
    Serial.println("Listing files stored on SD");

    File root = SDM.open(folder);
    uploadFolder = folder;

    while (true) {
        bool isDir;
        String fullPath = root.getNextFileName(&isDir);
        String nameOnly = fullPath.substring(fullPath.lastIndexOf("/") + 1);
        if (fullPath == "") { break; }
        // Serial.printf("Path: %s (isDir: %d)\n", fullPath.c_str(), isDir);

        if (esp_get_free_heap_size() > (String("Fo:" + nameOnly + ":0\n").length()) + 1024) {
            if (isDir) {
                // Serial.printf("Directory: %s\n", fullPath.c_str());
                returnText += "Fo:" + nameOnly + ":0\n";
            } else {
                // For files, we need to get the size, so we open the file briefly
                // Serial.printf("Opening file for size check: %s\n", fullPath.c_str());
                File fileForSize = SDM.open(fullPath);
                // Serial.printf("File size: %llu bytes\n", fileForSize.size());
                if (fileForSize) {
                    returnText += "Fi:" + nameOnly + ":" + humanReadableSize(fileForSize.size()) + "\n";
                    fileForSize.close();
                }
            }
        } else break;
        esp_task_wdt_reset();
    }
    root.close();

    // log_i("ListFiles End");
    return returnText;
}

std::map<String, unsigned long> sessions;
bool sessionTokenLoaded = false;
String persistedSessionToken;

void ensurePersistedSessionLoaded() {
    if (sessionTokenLoaded) return;
    sessionTokenLoaded = true;
    persistedSessionToken = loadSessionToken();
    if (!persistedSessionToken.isEmpty()) { sessions[persistedSessionToken] = millis(); }
}
// Generate random token
String generateToken(int length = 24) {
    String token = "";
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < length; i++) { token += charset[random(0, sizeof(charset) - 1)]; }
    return token;
}

/**********************************************************************
**  Function: serveWebUIFile
**  serves files for WebUI and checks for custom WebUI files
**********************************************************************/
void serveWebUIFile(
    AsyncWebServerRequest *request, String filename, const char *contentType, bool gzip,
    const uint8_t *originaFile, uint32_t originalFileSize
) {
    (void)filename;
    AsyncWebServerResponse *response =
        request->beginResponse_P(200, contentType, originaFile, originalFileSize);
    if (gzip) response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

/**********************************************************************
**  Function: checkUserWebAuth
** used by server->on functions to discern whether a user has the correct
** httpapitoken OR is authenticated by username and password
**********************************************************************/
bool checkUserWebAuth(AsyncWebServerRequest *request, bool onFailureReturnLoginPage = false) {
    ensurePersistedSessionLoaded();

    if (request->hasHeader("Cookie")) {
        const AsyncWebHeader *cookie = request->getHeader("Cookie");
        String c = cookie->value();
        int idx = c.indexOf("ESP32SESSION=");
        if (idx != -1) {
            int start = idx + 13;
            int end = c.indexOf(';', start);
            if (end == -1) end = c.length();
            String token = c.substring(start, end);
            auto it = sessions.find(token);
            if (it != sessions.end()) {
                it->second = millis();
                return true;
            }
        }
    }
    if (onFailureReturnLoginPage) {
        serveWebUIFile(request, "login.html", "text/html", true, login_html, login_html_size);
    } else {
        request->send(401, "text/plain", "Unauthorized");
    }
    return false;
}

// Função auxiliar para criar diretórios recursivamente
void createDirRecursive(String path) {
    String currentPath = "";
    int startIndex = 0;
    Serial.print("Verifying folder: ");
    Serial.println(path);

    while (startIndex < path.length()) {
        int endIndex = path.indexOf("/", startIndex);
        if (endIndex == -1) endIndex = path.length();

        currentPath += path.substring(startIndex, endIndex);
        if (currentPath.length() > 0) {
            if (!SDM.exists(currentPath)) {
                SDM.mkdir(currentPath);
                Serial.print("Creating folder: ");
                Serial.println(currentPath);
            }
        }

        if (endIndex < path.length()) { currentPath += "/"; }
        startIndex = endIndex + 1;
    }
}

bool runOnce = false;
// handles uploads to the filserver
void handleUpload(
    AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final
) {
    // make sure authenticated before allowing upload
    // Serial.println("Folder: " + uploadFolder);
    if (uploadFolder == "/") uploadFolder = "";

    if (checkUserWebAuth(request)) {
        if (!index || runOnce) {
            if (!update) {
                // Verifica se é um upload de pasta
                Serial.println("File: " + uploadFolder + "/" + filename);
                String relativePath = filename;
                String fullPath = uploadFolder + "/" + relativePath;
                // Cria diretórios necessários
                String dirPath = fullPath.substring(0, fullPath.lastIndexOf("/"));
                if (dirPath.length() > 0) { createDirRecursive(dirPath); }
            // Upload de arquivo único
            TRY_AGAIN:
                request->_tempFile = SDM.open(uploadFolder + "/" + filename, "w");
                if (!request->_tempFile) {
                    Serial.println("Fail creating file: " + String(filename));
                    vTaskDelay(5 / portTICK_PERIOD_MS);
                    goto TRY_AGAIN;
                }
            } else {
                runOnce = false;
                // open the file on first call and store the file handle in the request object
                if (Update.begin(file_size, command)) {
                    if (command == 0) prog_handler = 0;
                    else prog_handler = 1;

                    progressHandler(0, 500);
                    Update.onProgress(progressHandler);
                } else {
                    displayRedStripe("FAIL 160: " + String(Update.getError()));
                    delay(3000);
                }
            }
        }

        if (len) {
            // stream the incoming chunk to the opened file
            if (!update) {
                request->_tempFile.write(data, len);
            } else {
                if (!Update.write(data, len)) displayRedStripe("FAIL 170");
            }
        }

        if (final) {
            if (!update) {
                // close the file handle as the upload is now done
                request->_tempFile.close();
                request->redirect("/");
            } else {

                if (!Update.end()) {
                    displayRedStripe("Fail 181: " + String(Update.getError()));
                    delay(3000);
                } else {
                    request->send(200, "text/plain", "OK");
                    displayRedStripe("Restart your device");
                }
            }
        }
    } else {
        return request->requestAuthentication();
    }
}

void notFound(AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); }

void configureWebServer() {
    ensurePersistedSessionLoaded();

    // configure web server

    MDNS.begin(host);
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    // if url isn't found
    server->onNotFound([](AsyncWebServerRequest *request) { request->redirect("/"); });

    // Login
    server->on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("username", true) && request->hasParam("password", true)) {
            String username = request->getParam("username", true)->value();
            String password = request->getParam("password", true)->value();

            if (username == wui_usr && password == wui_pwd) {
                String token = generateToken();
                sessions.clear();
                sessions[token] = millis();
                saveSessionToken(token);
                sessionTokenLoaded = true;
                persistedSessionToken = token;

                AsyncWebServerResponse *response = request->beginResponse(302);
                response->addHeader("Location", "/");
                response->addHeader("Set-Cookie", "ESP32SESSION=" + token + "; Path=/; HttpOnly");
                request->send(response);
                return;
            }
        }
        AsyncWebServerResponse *response = request->beginResponse(302);
        response->addHeader("Location", "/?failed");
        request->send(response);
    });

    // Logout
    server->on("/logout", HTTP_GET, [](AsyncWebServerRequest *request) {
        ensurePersistedSessionLoaded();
        if (request->hasHeader("Cookie")) {
            const AsyncWebHeader *cookie = request->getHeader("Cookie");
            String c = cookie->value();
            int idx = c.indexOf("ESP32SESSION=");
            if (idx != -1) {
                int start = idx + 13;
                int end = c.indexOf(';', start);
                if (end == -1) end = c.length();
                String token = c.substring(start, end);
                sessions.erase(token);
                saveSessionToken("");
                sessionTokenLoaded = true;
                persistedSessionToken = "";
            }
        }
        AsyncWebServerResponse *response = request->beginResponse(302);
        response->addHeader("Location", "/?loggedout");
        response->addHeader("Set-Cookie", "ESP32SESSION=0; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
        request->send(response);
    });

    // presents a "you are now logged out webpage
    server->on("/logged-out", HTTP_GET, [](AsyncWebServerRequest *request) {
        String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
        Serial.println(logmessage);
#ifdef PART_04MB
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", "", 0);
        request->send(response);
#else
        serveWebUIFile(request, "logout.html", "text/html", true, logout_html, logout_html_size);
#endif
    });

    server->on("/UPDATE", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (checkUserWebAuth(request)) {
            if (request->hasParam("fileName", true)) {
                fileToCopy = request->getParam("fileName", true)->value().c_str();
                request->send(200, "text/plain", "Starting Update");
                updateFromSd_var = true;
            }
        }
    });

    server->on("/rename", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (checkUserWebAuth(request)) {
            if (request->hasParam("fileName", true) && request->hasParam("filePath", true)) {
                String fileName = request->getParam("fileName", true)->value().c_str();
                String filePath = request->getParam("filePath", true)->value().c_str();
                String filePath2 = filePath.substring(0, filePath.lastIndexOf('/') + 1) + fileName;
                if (!setupSdCard()) {
                    request->send(200, "text/plain", "Fail starting SD Card.");
                } else {
                    // Rename the file of folder
                    if (SDM.rename(filePath, filePath2)) {
                        request->send(200, "text/plain", filePath + " renamed to " + filePath2);
                    } else {
                        request->send(200, "text/plain", "Fail renaming file.");
                    }
                }
            }
        }
    });
    server->on("/OTAFILE", HTTP_POST, [](AsyncWebServerRequest *request) {}, handleUpload);

    server->on("/OTA", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (checkUserWebAuth(request)) {
            if (request->hasParam("update", true)) {
                update = true;
                request->send(200, "text/plain", "Update");
            }

            if (request->hasParam("command", true)) {
                command = request->getParam("command", true)->value().toInt();
                if (request->hasParam("size", true)) {
                    file_size = request->getParam("size", true)->value().toInt();
                    if (file_size > 0) {
                        update = true;
                        runOnce = true;
                        request->send(200, "text/plain", "OK");
                    }
                }
            }
        }
    });
    // run handleUpload function when any file is uploaded
    server->onFileUpload(handleUpload);

    server->on("/scripts.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        serveWebUIFile(request, "scripts.js", "application/javascript", true, scripts_js, scripts_js_size);
    });

    server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
#ifdef PART_04MB
        serveWebUIFile(request, "style.css", "text/css", true, style_4mb_css, style_4mb_css_size);
#else
        serveWebUIFile(request, "style.css", "text/css", true, style_css, style_css_size);
#endif
    });
    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (checkUserWebAuth(request, true)) {
            serveWebUIFile(request, "index.html", "text/html", true, index_html, index_html_size);
        }
    });
    server->on("/systeminfo", HTTP_GET, [](AsyncWebServerRequest *request) {
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
        request->send(200, "application/json", response_body);
    });
    server->on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (checkUserWebAuth(request)) {
            shouldReboot = true;
            request->send(200, "text/html", "Rebooting");
        } else {
            return request->requestAuthentication();
        }
    });

    server->on("/listfiles", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (checkUserWebAuth(request)) {
            update = false;
            String folder = "";
            if (request->hasParam("folder")) {
                folder = request->getParam("folder")->value().c_str();
            } else {
                String folder = "/";
            }
            request->send(200, "text/plain", listFiles(folder));

        } else {
            return request->requestAuthentication();
        }
    });

    server->on("/file", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (checkUserWebAuth(request)) {
            if (request->hasParam("name") && request->hasParam("action")) {
                const char *fileName = request->getParam("name")->value().c_str();
                const char *fileAction = request->getParam("action")->value().c_str();

                if (!SDM.exists(fileName)) {
                    if (strcmp(fileAction, "create") == 0) {
                        // log_i("New Folder: %s",fileName);
                        if (!SDM.mkdir(fileName)) {
                            request->send(200, "text/plain", "FAIL creating folder: " + String(fileName));
                        } else {
                            request->send(200, "text/plain", "Created new folder: " + String(fileName));
                        }
                    } else {
                        request->send(400, "text/plain", "ERROR: file does not exist");
                    }
                } else {
                    if (strcmp(fileAction, "download") == 0) {
                        request->send(SDM, fileName, "application/octet-stream");
                    } else if (strcmp(fileAction, "delete") == 0) {
                        if (deleteFromSd(fileName)) {
                            request->send(200, "text/plain", "Deleted : " + String(fileName));
                        } else {
                            request->send(200, "text/plain", "FAIL delating: " + String(fileName));
                        }

                    } else if (strcmp(fileAction, "create") == 0) {
                        // i("Folder Exists: %s",fileName);
                        if (SDM.mkdir(fileName)) {
                        } else {
                            request->send(
                                200, "text/plain", "FAIL creating existing folder: " + String(fileName)
                            );
                        }
                        request->send(200, "text/plain", "Created new folder: " + String(fileName));
                    } else {
                        request->send(400, "text/plain", "ERROR: invalid action param supplied");
                    }
                }
            } else {
                request->send(400, "text/plain", "ERROR: name and action params required");
            }
        } else {
            return request->requestAuthentication();
        }
    });

    server->on("/sdpins", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (checkUserWebAuth(request)) {
            if (request->hasParam("miso") && request->hasParam("mosi") && request->hasParam("sck") &&
                request->hasParam("cs")) {
#if defined(HEADLESS)
                int miso = request->getParam("miso")->value().toInt();
                int mosi = request->getParam("mosi")->value().toInt();
                int sck = request->getParam("sck")->value().toInt();
                int cs = request->getParam("cs")->value().toInt();

                // Verifica se os pinos são válidos (entre 0 e 44)
                if (miso > 44 || mosi > 44 || sck > 44 || cs > 44 || miso < 0 || mosi < 0 || sck < 0 ||
                    cs < 0) {
                    request->send(200, "text/plain", "Pins not configured.");
                    goto error;
                }

                // Grava os valores no EEPROM
                _sck = sck;
                _miso = miso;
                _mosi = mosi;
                _cs = cs;
                saveIntoNVS();
                setupSdCard();
                request->send(200, "text/plain", "Pins configured.");
            error:
                vTaskDelay(pdTICKS_TO_MS(1));
#else
        request->send(200, "text/plain", "Functionality exclusive for Headless environment (devices with no screen)");
#endif
            }
        } else {
            return request->requestAuthentication();
        }
    });

    server->on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (checkUserWebAuth(request)) {
            if (request->hasParam("usr") && request->hasParam("pwd")) {
                const char *usr = request->getParam("usr")->value().c_str();
                const char *pwdd = request->getParam("pwd")->value().c_str();
                wui_pwd = pwdd;
                wui_usr = usr;
                saveConfigs();
                config.httpuser = usr;
                config.httppassword = pwdd;

                request->send(
                    200, "text/plain", "User: " + String(ssid) + " configured with password: " + String(pwd)
                );
            } else if (request->hasParam("ssid") && request->hasParam("pwd")) {
                const char *ssidd = request->getParam("ssid")->value().c_str();
                const char *pwdd = request->getParam("pwd")->value().c_str();
                pwd = pwdd;
                ssid = ssidd;
                if (setWifiCredential(ssid, pwd)) {
                    Serial.printf("WebUI: ssid->%s, pwd->%s\n", ssid.c_str(), pwd.c_str());
                    saveConfigs();
                } else {
                    Serial.println("WebUI: failed to store new WiFi entry");
                }
            }
        } else {
            return request->requestAuthentication();
        }
    });
}

String readLineFromFile(File myFile) {
    String line = "";
    char character;

    while (myFile.available()) {
        character = myFile.read();
        if (character == ';') { break; }
        line += character;
    }
    return line;
}

#ifndef HEADLESS
void startWebUi(String ssid, int encryptation, bool mode_ap) {
#ifdef E_PAPER_DISPLAY
    tft->stopCallback();
#endif
    file_size = 0;
    // log_i("Recovering User info from config.conf");
    getConfigs();
    config.httpuser = wui_usr;
    config.httppassword = wui_pwd;
    config.webserverporthttp = default_webserverporthttp;

    // log_i("Connecting to WiFi");
    if (WiFi.status() != WL_CONNECTED) {
        // Choose wifi access mode
        wifiConnect(ssid, encryptation, mode_ap);
    }

    // configure web server
    // log_i("Configuring WebServer");
    Serial.println("Configuring Webserver ...");
    server = new AsyncWebServer(config.webserverporthttp);
    configureWebServer();

    // startup web server
    server->begin();
    vTaskDelay(pdTICKS_TO_MS(500));

    tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, ALCOLOR);
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
    setTftDisplay(7, 7, ALCOLOR, FP, BGCOLOR);
    tft->drawCentreString("-= Launcher WebUI =-", tftWidth / 2, 0, 8);
    String txt;
    if (!mode_ap) txt = WiFi.localIP().toString();
    else txt = WiFi.softAPIP().toString();

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
#ifdef E_PAPER_DISPLAY
    tft->startCallback();
#endif

    while (!check(SelPress)) {
        if (shouldReboot) {
            FREE_TFT
#if CONFIG_IDF_TARGET_ESP32P4
            const esp_partition_t *partition =
                esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
            esp_ota_set_boot_partition(partition);
            ESP.deepSleep(100);
#endif
            ESP.restart();
        }
        // Perform installation from SD Card
        if (updateFromSd_var) {
            // log_i("Starting Update from SD");
            updateFromSD(fileToCopy);
            updateFromSd_var = false;
            fileToCopy = "";
            displayRedStripe("Restart your Device");
        }
    }

    // log_i("Closing Server and turning off WiFi");
    server->reset();
    server->end();
    vTaskDelay(pdTICKS_TO_MS(100));
    delete server;
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);

    tft->fillScreen(BGCOLOR);
}

#else

void startWebUi(String ssid, int encryptation, bool mode_ap) {
    file_size = 0;

    config.httpuser = wui_usr;
    config.httppassword = wui_pwd;
    config.webserverporthttp = default_webserverporthttp;

    if (WiFi.status() != WL_CONNECTED) {
        // Choose wifi access mode
        wifiConnect(ssid, encryptation, mode_ap);
    }

    // configure web server
    // log_i("Configuring WebServer");
    Serial.println("Configuring Webserver ...");
    server = new AsyncWebServer(config.webserverporthttp);
    configureWebServer();

    // startup web server
    server->begin();
    vTaskDelay(pdTICKS_TO_MS(500));

    String txt;
    if (!mode_ap) txt = WiFi.localIP().toString();
    else txt = WiFi.softAPIP().toString();

    Serial.println("Access: http://launcher.local");
    Serial.print("IP ");
    Serial.println(txt);
    Serial.println("Usr: " + String(wui_usr));
    Serial.println("Pwd: " + String(wui_pwd));

    while (1) {
        if (shouldReboot) {
            FREE_TFT
            ESP.restart();
        }
        // Perform installation from SD Card
        if (updateFromSd_var) {
            // log_i("Starting Update from SD");
            updateFromSD(fileToCopy);
            updateFromSd_var = false;
            fileToCopy = "";
            Serial.println("\n\n--------------------\nRestart your Device");
        }
    }

    log_i("Closing Server and turning off WiFi, something went wrong?");
    server->reset();
    server->end();
    vTaskDelay(pdTICKS_TO_MS(100));
    delete server;
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
}

#endif
