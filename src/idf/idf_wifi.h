#ifndef LAUNCHER_IDF_WIFI_H
#define LAUNCHER_IDF_WIFI_H

#include "esp_wifi_types.h"
#include <stdint.h>
#include <string>
#include <vector>

struct LauncherWifiAp {
    std::string ssid;
    wifi_auth_mode_t authmode;
    int8_t rssi;
};

bool launcherWifiStartSta();
bool launcherWifiInitHostedSdio(
    int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t rst
);
bool launcherWifiConnect(const char *ssid, const char *password, uint32_t timeout_ms);
int launcherWifiScan(std::vector<LauncherWifiAp> &out);
bool launcherWifiStartAp(const char *ssid, const char *password, uint8_t channel, uint8_t max_clients);
bool launcherWifiStop();
bool launcherWifiIsConnected();
std::string launcherWifiLocalIp();
std::string launcherWifiApIp();
std::string launcherWifiMac();
bool launcherMdnsStart(const char *host, uint16_t port = 80);
void launcherMdnsStop();

#endif
