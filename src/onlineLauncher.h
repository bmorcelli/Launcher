#ifndef __ONLINELAUNCHER_H
#define __ONLINELAUNCHER_H

#include <ArduinoJson.h>
#include <EEPROM.h>
#include <HTTPClient.h>
#include <M5-HTTPUpdate.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

bool installExtFirmware(String url);

void installFirmware(
    String fid, String file, uint32_t app_size, bool spiffs, uint32_t spiffs_offset, uint32_t spiffs_size,
    bool nb, bool fat, uint32_t fat_offset[2], uint32_t fat_size[2]
);

void connectWifi();

void ota_function();

void downloadFirmware(String fid, String file, String fileName, String folder = "/downloads/");

void wifiConnect(String ssid, int encryptation, bool isAP = false);

bool GetJsonFromLauncherHub(
    uint8_t page = 1, String order = "downloads", bool star = false, String query = ""
);

JsonDocument getVersionInfo(String fid);

bool installFAT_OTA(WiFiClientSecure *client, String file, uint32_t offset, uint32_t size, const char *label);

#endif
