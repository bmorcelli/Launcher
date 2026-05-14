#include "onlineLauncher.h"
#include "display.h"
#include "idf/idf_http_client.h"
#include "idf/idf_update.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
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

bool launcherUpdateHttpCb(const uint8_t *data, size_t len, void *ctx) {
    HttpUpdateContext *updateCtx = static_cast<HttpUpdateContext *>(ctx);
    if (!updateCtx) return false;
    if (!updateCtx->started) {
        if (!launcherUpdateBegin(updateCtx->target, updateCtx->expected)) return false;
        updateCtx->started = true;
        progressHandler(0, updateCtx->expected);
    }
    size_t wrote = launcherUpdateWrite(data, len);
    if (wrote != len) return false;
    updateCtx->written += wrote;
    progressHandler(updateCtx->written, updateCtx->expected);
    return true;
}

bool getInfo(String serverUrl, JsonDocument &_doc) {
    if (launcherWifiIsConnected()) {
        vTaskSuspend(xHandle);
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
                    vTaskResume(xHandle);
                    return false;
                }
                launcherConsolePrintf("[GetInfo] Downloaded and parsed json with size: %d\n", _doc.size());
                vTaskResume(xHandle);
                return true;
            }

            launcherConsolePrintf("[GetInfo] HTTP fetch failed: %s\n", serverUrl.c_str());
            tftprint(".", 10);
            vTaskDelay(pdTICKS_TO_MS(500));
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
    vTaskSuspend(xHandle);
    FileDownloadContext download = {&file, 0, 0, 0, &response};
    bool ok = launcherHttpGetStream(
        fileAddr.c_str(), fileDownloadCb, &download, &response, "HWID", launcherWifiMac().c_str()
    );
    file.flush();
    file.close();
    vTaskResume(xHandle);

    vTaskDelay(pdTICKS_TO_MS(50));
    file = SDM.open(filePath, FILE_READ);
    size_t sdSize = file ? file.size() : 0;
    if (file) file.close();
    if ((!ok || sdSize <= bufSize) && tries < 1) {
        tries++;
        SDM.remove(filePath);
        goto retry;
    }
    launcherConsolePrintf("File size in get() = %d\nFile size in SD    = %d\n", (int)response.content_length, sdSize);
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

    /* Install App */
    prog_handler = 0;
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
    progressHandler(0, 500);
    vTaskSuspend(xHandle);
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
    vTaskResume(xHandle);
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
    bool ok = launcherHttpGetRange(file.c_str(), offset, size, launcherUpdateHttpCb, &fatUpdate) &&
              launcherUpdateEnd();
    vTaskDelay(pdTICKS_TO_MS(500));
    return ok;
}

#endif
