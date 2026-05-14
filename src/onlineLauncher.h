#ifndef __ONLINELAUNCHER_H
#define __ONLINELAUNCHER_H

#include <ArduinoJson.h>
#include <SPIFFS.h>

bool installExtFirmware(String url);

void installFirmware(
    String fid, String file, uint32_t app_size, uint32_t app_offset, bool spiffs, uint32_t spiffs_offset,
    uint32_t spiffs_size, bool nb, bool fat, uint32_t fat_offset[2], uint32_t fat_size[2], String installedName = ""
);

void connectWifi();

void ota_function();

void downloadFirmware(String fid, String file, String fileName, String folder = "/downloads/");

void wifiConnect(String ssid, int encryptation, bool isAP = false);

bool GetJsonFromLauncherHub(
    uint8_t page = 1, String order = "downloads", bool star = false, String query = ""
);

JsonDocument getVersionInfo(String fid);

bool installFAT_OTA(String file, uint32_t offset, uint32_t size, const char *label);

bool clearOnlineCoredump();

#endif
