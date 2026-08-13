#ifndef LAUNCHER_IDF_WIFI_AT_H
#define LAUNCHER_IDF_WIFI_AT_H

// ESP-AT-over-SDIO backend for boards whose Wi-Fi co-processor may be running LilyGO's
// factory ESP-AT firmware instead of an ESP-Hosted slave (see idf_wifi.cpp for the
// dispatch between the two). Only compiled when ENABLE_ESP_AT_INTERFACE is defined -
// everywhere else this header/its .cpp are inert, so boards using the Hosted-only path
// pay no cost.
#if defined(ENABLE_ESP_AT_INTERFACE)

#include "idf_http_client.h"
#include "idf_wifi.h"
#include <stdint.h>

enum class LauncherC6Firmware : uint8_t { kNoResponse, kEspAt, kOther };

// Brings the SDIO bus up with the plain ESP-IDF sdmmc driver (no ESP-Hosted involved)
// and identifies what firmware answers on it, then tears the bus back down. Safe to
// call before deciding which backend (Hosted vs AT) to actually use; leaves the C6
// freshly reset either way so whichever backend runs next gets a clean slave.
LauncherC6Firmware launcherWifiAtProbe(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3);

// Brings the AT bus up for real use and leaves it attached (unlike the probe above).
// Waits for the firmware's boot "ready" banner, disables command echo, and puts the
// co-processor in station mode. Returns false if it never answers.
bool launcherWifiAtInit(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3);

LauncherWifiConnectState
launcherWifiAtConnectStatus(const char *ssid, const char *password, uint32_t timeout_ms);
bool launcherWifiAtConnect(const char *ssid, const char *password, uint32_t timeout_ms);
bool launcherWifiAtDisconnect();
int launcherWifiAtScan(std::vector<LauncherWifiAp> &out);
bool launcherWifiAtIsConnected();
std::string launcherWifiAtLocalIp();
std::string launcherWifiAtMac();

// HTTP over AT+HTTPCLIENT. The whole response body is assembled in RAM by the C6 before
// AT hands it back in one +HTTPCLIENT:<size>,<data> block, so this is only suitable for
// modest bodies/chunks. Large downloads use launcherWifiAtHttpGetStreamRaw() instead.
// rangeSize == 0 means "no Range header, plain GET".
bool launcherWifiAtHttpGet(
    const char *url, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *response,
    uint32_t rangeOffset = 0, uint32_t rangeSize = 0, const char *headerKey = nullptr,
    const char *headerValue = nullptr
);
bool launcherWifiAtHttpGetStreamRaw(
    const char *url, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *response,
    const char *headerKey = nullptr, const char *headerValue = nullptr
);
bool launcherWifiAtHttpPost(
    const char *url, const char *body, size_t bodyLen, String &out, size_t maxSize,
    LauncherHttpResponse *response
);

#endif // ENABLE_ESP_AT_INTERFACE
#endif // LAUNCHER_IDF_WIFI_AT_H
