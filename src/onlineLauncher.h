#ifndef __ONLINELAUNCHER_H
#define __ONLINELAUNCHER_H

#include "partition_install_layout.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>

bool installExtFirmware(const String &url);

void installFirmware(
    String fid, String file, uint32_t app_size, uint32_t app_offset, bool nb,
    std::vector<LauncherInstallDataPartition> &dataPartitions, String installedName = ""
);
void installFirmwareFromManifest(const String &fid, const String &version, String installedName = "");

bool connectWifi();
bool ensureWifiConnected(const String &ssid = "", int encryptation = 0, bool isAP = false);

void ota_function();

void downloadFirmware(const String &fid, String file, String fileName, String folder = "/downloads/", const String &version = "", bool autoAdvance = false);
void saveDownloadedFirmware(const String &folder, const String &fid, const String &version);
bool checkForUpdates();

bool wifiConnect(const String &ssid, int encryptation, bool isAP = false);

bool GetJsonFromLauncherHub(
    uint8_t page = 1, const String &order = "downloads", bool star = false, const String &query = ""
);

JsonDocument getVersionInfo(const String &fid);

bool installFAT_OTA(String file, uint32_t offset, uint32_t size, const char *label);

#endif
