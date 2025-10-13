#include "onlineLauncher.h"
#include "display.h"
#include "mykeyboard.h"
#include "powerSave.h"
#include "sd_functions.h"
#include "settings.h"
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
        JsonObject setting = settings[0];
        JsonArray WifiList = setting["wifi"].as<JsonArray>();
        if (WifiList.isNull()) { WifiList = setting.createNestedArray("wifi"); }
        EEPROM.begin(EEPROMSIZE);
        pwd = EEPROM.readString(20);
        EEPROM.end();
        Serial.printf("sdcardMounted: %d\n", sdcardMounted);

        if (sdcardMounted) {
            for (JsonObject wifiEntry : WifiList) {
                String name = wifiEntry["ssid"].as<String>();
                String pass = wifiEntry["pwd"].as<String>();
                Serial.printf("SSID: %s, Pass: %s\n", name, pass);
                if (name == ssid) {
                    pwd = pass;
                    found = true;
                    Serial.printf("Found SSID: %s\n", name);
                    break;
                }
            }
        }

    Retry:
        if (!found || wrongPass) {
            if (encryptation > 0) {
                pwd = keyboard(pwd, 63, "Network Password:");
                if (pwd == String(KEY_ESCAPE)) {
                    returnToMenu = true;
                    goto END;
                }
            }

            EEPROM.begin(EEPROMSIZE);
            if (pwd != EEPROM.readString(20)) {
                EEPROM.writeString(20, pwd);
                EEPROM.commit(); // Store data to EEPROM
            }
            EEPROM.end(); // Free EEPROM memory
            if (sdcardMounted && !found) {
                JsonObject newWifi = WifiList.add<JsonObject>();
                if (newWifi.isNull()) { newWifi = WifiList.add<JsonObject>(); }
                if (!newWifi.isNull()) {
                    newWifi["ssid"] = ssid;
                    newWifi["pwd"] = pwd;
                    found = true;
                    saveConfigs();
                } else {
                    log_e("wifiConnect: failed to store new WiFi entry");
                }
            } else if (sdcardMounted && found && wrongPass) {
                for (JsonObject wifiEntry : WifiList) {
                    if (wifiEntry["ssid"].as<String>() == ssid) {
                        wifiEntry["pwd"] = pwd;
                        Serial.printf("Mudou pwd de SSID: %s\n", ssid);
                        break;
                    }
                }
                saveConfigs();
            }
        }

        WiFi.begin(ssid.c_str(), pwd.c_str());

        resetTftDisplay(10, 10, FGCOLOR, FP);
        tft->fillScreen(BGCOLOR);
        tftprint("Connecting to: " + ssid + ".", 10);
        tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
        // Simulação da função de desenho no display TFT
        int count = 0;
        while (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            tftprint(".", 10);
            count++;
            if (count > 20) {
                wrongPass = true;
                options = {
                    {"Retry",     [&]() { yield(); }            },
                    {"Main Menu", [&]() { returnToMenu = true; }},
                };
                loopOptions(options);
                if (!returnToMenu) goto Retry;
                else goto END;
            }
        }
    } else { // Running in Access point mode
        IPAddress AP_GATEWAY(172, 0, 0, 1);
        WiFi.mode(WIFI_AP);
#if CONFIG_ESP_HOSTED_ENABLED
        vTaskDelay(500 / portTICK_PERIOD_MS);
#endif
        WiFi.softAPConfig(AP_GATEWAY, AP_GATEWAY, IPAddress(255, 255, 255, 0));
        WiFi.softAP("Launcher", "", 6, 0, 1, false);
        Serial.print("IP: ");
        Serial.println(WiFi.softAPIP());
    }
END:
    delay(0);
}
void connectWifi() {
    int nets;
    // WiFi.disconnect(true);
    WiFi.mode(WIFI_MODE_STA);
    displayRedStripe("Scanning...");
#if CONFIG_ESP_HOSTED_ENABLED
    vTaskDelay(500 / portTICK_PERIOD_MS);
#endif
    nets = WiFi.scanNetworks();
    options = {};
    for (int i = 0; i < nets; i++) {
        options.push_back({WiFi.SSID(i).c_str(), [=]() {
                               wifiConnect(WiFi.SSID(i).c_str(), int(WiFi.encryptionType(i)));
                           }});
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
    if (WiFi.status() != WL_CONNECTED) connectWifi();
    if (WiFi.status() == WL_CONNECTED) {
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
        RELOAD:
            options.clear();
            for (JsonObject item : favorite) {
                if (item["fid"].as<String>().length() > 0) {
                    options.push_back({item["name"].as<String>(), [=]() {
                                           loopVersions(item["fid"].as<String>());
                                       }});
                } else {
                    options.push_back({item["name"].as<String>(), [=]() {
                                           installExtFirmware(item["link"].as<String>());
                                       }});
                }
            }
            options.push_back({"Main Menu", [=]() { returnToMenu = true; }, ALCOLOR});
            idx = loopOptions(options, false, FGCOLOR, BGCOLOR, false, idx);
            if (!returnToMenu) goto RELOAD;
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
    const char charsToReplace[] = {'/', '\\', '\"', '\'', '`'};
    // Define o caractere de substituição (neste exemplo, usamos um espaço)
    const char replacementChar = '_';

    // Percorre a string e substitui os caracteres especificados
    for (size_t i = 0; i < sizeof(charsToReplace); i++) {
        input.replace(String(charsToReplace[i]), String(replacementChar));
    }
    return input;
}

bool getInfo(String serverUrl, JsonDocument &_doc) {
    if (WiFi.status() == WL_CONNECTED) {
        vTaskSuspend(xHandle);
        WiFiClientSecure *client = new WiFiClientSecure();
        client->setInsecure();
        HTTPClient http;
        resetTftDisplay();
        tft->drawRoundRect(5, 5, tftWidth - 10, tftHeight - 10, 5, FGCOLOR);
        tft->drawCentreString("Getting info from", tftWidth / 2, tftHeight / 3, 1);
        tft->drawCentreString("LauncherHub", tftWidth / 2, tftHeight / 3 + FM * 9, 1);

        tft->setCursor(18, tftHeight / 3 + FM * 9 * 2);
        const uint8_t maxAttempts = 5;
        for (uint8_t attempt = 0; attempt < maxAttempts; ++attempt) {
            if (!http.begin(*client, serverUrl)) {
                Serial.printf("[GetInfo] Unable to reach %s\n", serverUrl);
                break;
            }
            http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
            http.useHTTP10(true);
            int httpResponseCode = http.GET();
            if (httpResponseCode == HTTP_CODE_OK) {
                String payload = http.getString();
                http.end();
                _doc.clear();
                DeserializationError error = deserializeJson(_doc, payload);
                if (error) {
                    Serial.printf("[GetInfo] Failed to parse JSON: %s\n", error.c_str());
                    displayRedStripe("JSON Parse Failed");
                    vTaskDelay(1500 / portTICK_PERIOD_MS);
                    _doc.clear();
                    vTaskResume(xHandle);
                    return false;
                }
                Serial.printf("[GetInfo] Downloaded and parsed json with size: %d\n", _doc.size());
                vTaskResume(xHandle);
                return true;
            }

            Serial.printf("[GetInfo] HTTP error: %d\n", httpResponseCode);
            tftprint(".", 10);
            http.end();
        }
    }
    vTaskResume(xHandle);
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
    String serverUrl = "https://api.launcherhub.net/firmwares?category=" + String(OTA_TAG) + q;

    if (getInfo(serverUrl, doc)) {
        total_firmware = doc["total"].as<int>();
        num_pages = doc["total"].as<int>() / doc["page_size"].as<int>();
        current_page = page;
        Serial.printf("GetJsonFromLauncherHub> Loaded %d firmwares\n", total_firmware);
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
/***************************************************************************************
** Function name: downloadFirmware
** Description:   Downloads the firmware and save into the SDCard
***************************************************************************************/
void downloadFirmware(String fid, String file, String fileName, String folder) { // Adicionar "fid"
    if (!file.startsWith("https://")) file = M5_SERVER_PATH + file;
    String fileAddr = "https://api.launcherhub.net/download?fid=" + fid + "&file=" + file;
    if (fid == "") fileAddr = file;
    int tries = 0;
    fileName = replaceChars(fileName);
    prog_handler = 2;
    if (!setupSdCard()) {
        displayRedStripe("SDCard Not Found");
        delay(2500);
        return;
    }

    tft->fillRect(7, 40, tftWidth - 14, 88, BGCOLOR); // Erase the information below the firmware name
    displayRedStripe("Connecting FW");
    WiFiClientSecure *client = new WiFiClientSecure;
    client->setInsecure();
retry:
    if (client) {
        HTTPClient http;
        int httpResponseCode = -1;

        while (httpResponseCode < 0) {
            http.begin(*client, fileAddr);
            http.addHeader("HWID", WiFi.macAddress());
            http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // Github links need it
            http.useHTTP10(true);
            httpResponseCode = http.GET();
            if (httpResponseCode > 0) {
                setupSdCard();
                if (!SDM.exists("/downloads")) SDM.mkdir("/downloads");

                File file = SDM.open(folder + fileName + ".bin", FILE_WRITE, true);
                size_t size = http.getSize();
                displayRedStripe("Downloading FW");
                if (file) {
                    vTaskSuspend(xHandle);
                    int downloaded = 0;
                    WiFiClient *stream = http.getStreamPtr();
                    int len = size;

                    prog_handler = 2; // Download handler

                    // Ler dados enquanto disponível
                    progressHandler(downloaded, size);
                    while (http.connected() && (len > 0 || len == -1)) {
                        // Ler dados em partes
                        int size_av = stream->available();
                        if (size_av) {
                            int c = stream->readBytes(buff, size_av < bufSize ? size_av : bufSize);
                            if (c <= 0) continue;
                            size_t wrote = file.write(buff, c);
                            if (wrote != static_cast<size_t>(c)) {
                                log_i("Download> write failed after %d bytes", downloaded);
                                break;
                            }

                            if (len > 0) { len -= c; }

                            downloaded += c;
                            tft->drawPixel(0, 0, 0);
                            progressHandler(downloaded, size); // Chama a função de progresso
                        }
                    }
                    file.flush();
                    file.close();
                    vTaskResume(xHandle);
                } else {
                    Serial.printf("Download> Couldn't create file %s\n", String(folder + fileName + ".bin"));
                    displayRedStripe("Fail creating file.");
                }
                // Checks if the file was preatically not downloaded and try one more time (size <=
                // bufSize)
                vTaskDelay(pdTICKS_TO_MS(50));
                file = SDM.open(folder + fileName + ".bin");
                if (file.size() <= bufSize & tries < 1) {
                    tries++;
                    http.end();
                    goto retry;
                }
                // Checks if the file was completely downloaded
                Serial.printf("File size in get() = %d\nFile size in SD    = %d\n", size, file.size());
                if (file.size() != size) {
                    SDM.remove(file.path());
                    displayRedStripe("Download FAILED");
                    while (!check(SelPress)) yield();
                } else {
                    Serial.printf("File successfully downloaded.\n");
                    displayRedStripe(" Downloaded ");
                    while (!check(SelPress)) yield();
                }
                file.close();
                break;
            } else {
                http.end();
                vTaskDelay(pdTICKS_TO_MS(500));
            }
        }
        http.end();
    } else {
        displayRedStripe("Couldn't Connect");
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
    WiFiClientSecure *client = new WiFiClientSecure;
    client->setInsecure();
    if (!client) return false;

    HTTPClient http;
    http.begin(*client, url);
    http.addHeader("Range", "bytes=32768-33183");          // Get the partition table
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // Github links need it
    http.useHTTP10(true);
    const char *headerKeys[] = {"Content-Range"};
    const size_t headerKeysCount = sizeof(headerKeys) / sizeof(headerKeys[0]);
    http.collectHeaders(headerKeys, headerKeysCount);
    if (http.GET() != 206) {
        displayRedStripe("File not found");
        http.end();
        return false;
    }
    String _fileSize = http.header("Content-Range");
    _fileSize = _fileSize.substring(_fileSize.lastIndexOf("/") + 1);
    file_size = _fileSize.toInt();

    WiFiClient *stream = http.getStreamPtr();
    int size_av = stream->available();
    stream->readBytes(buff, size_av < bufSize ? size_av : bufSize);
    http.end();
    // Check if it is a valid partition table
    size_t PartitionSize = 0;
    if (buff[0] == 0xAA) {
        nb = 0;                                    // File with bootloader an partitions
        for (int i = 0x0; i <= 0x1A0; i += 0x20) { // Partition
            memcpy(bytes, &buff[i], 16);

            // https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/partition-tables.html ->
            // spiffs (0x82) is for SPIFFS Filesystem.

            // if (bytes[3] == 0xFF) Serial.println(": ------- END of Table ------- |");
            if (bytes[3] == 0x00 || (bytes[3] >= 0x10 && bytes[3] <= 0x1F)) {
                Serial.println(": Ota or Factory partition |");
                if (bytes[0x0A] > 0 && PartitionSize == 0) {
                    PartitionSize = (bytes[0x0A] << 16) | (bytes[0x0B] << 8) |
                                    bytes[0x0C]; // Write the size of app0 partition
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
                Serial.println(": FAT partition                |");
                int a = 0;
                if (fat_offset[0] != 0) a = 1;
                fat_offset[a] = (bytes[0x06] << 16) | (bytes[0x07] << 8) |
                                bytes[0x08]; // Write the offset of FAT partition
                bytes[0x0C] = 0;
                fat_size[a] =
                    (bytes[0x0A] << 16) | (bytes[0x0B] << 8) | bytes[0x0C]; // Write the size of FAT partition
            }
            if (bytes[3] == 0x82 || bytes[3] == 0x83) {
                Serial.println(": Spiffs/LittleFs partition    |");
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
            temp_size += 0x10000;
            if (file_size <= temp_size) {  // Check if the file is smaller than the app0 partition
                PartitionSize = file_size; // gets file size
                PartitionSize -= 0x10000;  // subtracts bootloader, partitions and other junks
            } else {
                PartitionSize = PartitionSize; // if file is greater then app0 partition+junk, it will
                                               // limit to app0 partition size
            }
        }
        // Check if there is room for spiffs in the file
        if (file_size < spiffs_offset) {
            Serial.printf(
                "\nError: file doesn't reach spiffs offset %d, to read spiffs.", spiffs_offset, HEX
            );
        } else {
            Serial.println("Preparing to copy spiffs...");
            // check size of the Spiffs Partition, if it fits in the launcher
            // If it is larger the the Launcher Spiffs Partition, cut it to the limit
            if (spiffs_size > MAX_SPIFFS) {
                spiffs_size = MAX_SPIFFS;
                temp_size = spiffs_offset + spiffs_size;
                if (file_size <= temp_size) { spiffs_size = file_size - spiffs_offset; }
                Serial.print("\nTotal spiffs size after crop: ");
                Serial.println(spiffs_size, HEX);
            }
        }
    }
    Serial.printf(
        "url: %s\nPartitionSize: %d, spiffs: %d, spiffs_offset: %d, spiffs_size: %d, nb: %d, fat: %d, "
        "fat_offset: "
        "%d, fat_size: %d",
        url.c_str(),
        PartitionSize,
        spiffs,
        spiffs_offset,
        spiffs_size,
        nb,
        fat,
        fat_offset,
        fat_size
    );
    installFirmware(
        "", url, PartitionSize, spiffs, spiffs_offset, spiffs_size, nb, fat, fat_offset, fat_size
    );
    return true;
}
/***************************************************************************************
** Function name: installFirmware
** Description:   installs Firmware using OTA
***************************************************************************************/
void installFirmware( // adicionar "fid"
    String fid, String file, uint32_t app_size, bool spiffs, uint32_t spiffs_offset, uint32_t spiffs_size, bool nb,
    bool fat, uint32_t fat_offset[2], uint32_t fat_size[2]
) {
    uint32_t app_offset = 0x10000;
    if (!file.startsWith("https://")) file = M5_SERVER_PATH + file;
    String fileAddr = "https://api.launcherhub.net/download?fid=" + fid + "&file=" + file;
    if (fid == "") fileAddr = file;

    // Release RAM Memory from Json Objects
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

    tft->fillRect(7, 40, tftWidth - 14, 88, BGCOLOR); // Erase the information below the firmware name
    displayRedStripe("Connecting FW");

    WiFiClientSecure *client = new WiFiClientSecure;

    client->setInsecure();
    httpUpdate.rebootOnUpdate(false);
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // Github links need it
    /* Install App */
    prog_handler = 0;
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
    progressHandler(0, 500);
    httpUpdate.onProgress(progressHandler);
    httpUpdate.setLedPin(LED, LED_ON);
    vTaskSuspend(xHandle);
    bool success = false;
    if (nb) success = httpUpdate.update(*client, fileAddr);
    else success = httpUpdate.updateFromOffset(*client, fileAddr, app_offset, app_size);
    if (!client) {
        displayRedStripe("Couldn't Connect to server");
        goto SAIR;
    }
    if (!success) {
        displayRedStripe("Instalation Failed");
        goto SAIR;
    }

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
            delay(2500);
        } else {
            displayRedStripe("Formatting SPIFFS");
            SPIFFS.format();
            SPIFFS.end();
        }
        displayRedStripe("Connecting SPIFFs");

        // Install Spiffs
        progressHandler(0, 500);
        httpUpdate.onProgress(progressHandler);

        if (!httpUpdate.updateSpiffsFromOffset(*client, file, spiffs_offset, spiffs_size)) {
            displayRedStripe("SPIFFS Failed");
            delay(2500);
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
                    if (!installFAT_OTA(client, file, fat_offset[i], fat_size[i], "sys")) {
                        displayRedStripe("FAT Failed");
                        delay(2500);
                    }
                } else {
                    if (!installFAT_OTA(client, file, fat_offset[i], fat_size[i], "vfs")) {
                        displayRedStripe("FAT Failed");
                        delay(2500);
                    }
                }
            }
        }
    }
#endif

Sucesso:
    esp_restart();

// Só chega aqui se der errado
SAIR:
    vTaskResume(xHandle);
    delay(2000);
}

/***************************************************************************************
** Function name: installFAT_OTA
** Description:   install FAT partition OverTheAir
***************************************************************************************/
bool installFAT_OTA(
    WiFiClientSecure *client, String file, uint32_t offset, uint32_t size, const char *label
) {
    prog_handler = 1; // review

    tft->fillRect(7, 40, tftWidth - 14, 88, BGCOLOR); // Erase the information below the firmware name
    displayRedStripe("Connecting FAT");

    if (client) {
        HTTPClient http;
        int httpResponseCode = -1;
        http.begin(*client, file);
        http.addHeader("Range", "bytes=" + String(offset) + "-" + String(offset + size - 1));
        http.useHTTP10(true);

        while (httpResponseCode < 0) {
            httpResponseCode = http.GET();
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        if (httpResponseCode > 0) {
            int size = http.getSize();
            displayRedStripe("Installing FAT");
            WiFiClient *stream = http.getStreamPtr();
            prog_handler = 1; // Download handler
            performFATUpdate(*stream, size, label);
        }
        http.end();
        vTaskDelay(pdTICKS_TO_MS(500));
        return true;
    } else {
        displayRedStripe("Couldn't Connect");
        delay(2000);
        return false;
    }
}

#endif
