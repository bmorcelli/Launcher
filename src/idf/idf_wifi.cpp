#include "idf_wifi.h"

#include "ram_profile.h"

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include <stdio.h>
#include <string.h>

#if CONFIG_ESP_HOSTED_ENABLED
#include "esp32-hal-hosted.h"
#endif

namespace {
constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;
constexpr EventBits_t WIFI_FAIL_BIT = BIT1;
constexpr EventBits_t WIFI_STARTED_BIT = BIT2;
constexpr int kWifiConnectRetryLimit = 5;
// Delay before a background reconnect attempt after a spontaneous drop, to avoid
// hammering esp_wifi_connect() in a tight loop when the AP is unavailable.
constexpr uint64_t kReconnectDelayUs = 3000000; // 3 s

EventGroupHandle_t wifiEvents = nullptr;
esp_netif_t *staNetif = nullptr;
esp_netif_t *apNetif = nullptr;
bool wifiInitialized = false;
bool handlersRegistered = false;
bool mdnsStarted = false;
bool expectingConnection = false; // true only after esp_wifi_connect() is called
int wifiConnectRetryCount = 0;
wifi_err_reason_t lastDisconnectReason = WIFI_REASON_UNSPECIFIED;
// true once we have successfully obtained an IP; enables transparent background
// reconnection on a spontaneous drop. Cleared when the user explicitly stops STA.
bool autoReconnect = false;
esp_timer_handle_t reconnectTimer = nullptr;

bool isWrongPasswordReason(wifi_err_reason_t reason) {
    return reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_802_1X_AUTH_FAILED ||
           reason == WIFI_REASON_AUTH_EXPIRE;
}

// Fires kReconnectDelayUs after a spontaneous disconnect. Reuses the credentials
// already stored in the Wi-Fi driver config, so no need to re-supply SSID/password.
void reconnectTimerCb(void *) {
    if (!wifiEvents) return;
    if (!autoReconnect || expectingConnection) return;
    if ((xEventGroupGetBits(wifiEvents) & WIFI_CONNECTED_BIT) != 0) return;
    esp_wifi_connect();
}

void scheduleReconnect() {
    if (!reconnectTimer) return;
    esp_timer_stop(reconnectTimer); // no-op (and harmless error) if not running
    esp_timer_start_once(reconnectTimer, kReconnectDelayUs);
}

void wifiEventHandler(void *, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
    if (!wifiEvents) return;
    if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected =
            reinterpret_cast<wifi_event_sta_disconnected_t *>(eventData);
        if (disconnected) lastDisconnectReason = static_cast<wifi_err_reason_t>(disconnected->reason);
        xEventGroupClearBits(wifiEvents, WIFI_CONNECTED_BIT);
        if (expectingConnection) {
            if (isWrongPasswordReason(lastDisconnectReason)) {
                expectingConnection = false;
                xEventGroupSetBits(wifiEvents, WIFI_FAIL_BIT);
            } else if (wifiConnectRetryCount < kWifiConnectRetryLimit) {
                ++wifiConnectRetryCount;
                esp_wifi_connect();
            } else {
                expectingConnection = false;
                xEventGroupSetBits(wifiEvents, WIFI_FAIL_BIT);
            }
        } else if (autoReconnect) {
            // Spontaneous drop after a successful connection (signal loss, AP
            // reboot, ...). Schedule a delayed, transparent reconnect.
            scheduleReconnect();
        }
    } else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(wifiEvents, WIFI_STARTED_BIT);
    } else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_STOP) {
        xEventGroupClearBits(wifiEvents, WIFI_STARTED_BIT | WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    } else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
        expectingConnection = false;
        wifiConnectRetryCount = 0;
        lastDisconnectReason = WIFI_REASON_UNSPECIFIED;
        autoReconnect = true; // we are connected; arm background reconnection
        xEventGroupClearBits(wifiEvents, WIFI_FAIL_BIT);
        xEventGroupSetBits(wifiEvents, WIFI_CONNECTED_BIT);
    }
}

bool okOrAlready(esp_err_t err) { return err == ESP_OK || err == ESP_ERR_INVALID_STATE; }

bool ensureWifiInitialized() {
    if (!wifiEvents) wifiEvents = xEventGroupCreate();
    if (!wifiEvents) return false;

    esp_err_t err;
    err = esp_netif_init();
    if (!okOrAlready(err)) return false;

    err = esp_event_loop_create_default();
    if (!okOrAlready(err)) return false;

    if (!staNetif) {
        // Try to get an already-existing STA netif (Arduino framework may have created it)
        staNetif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!staNetif) staNetif = esp_netif_create_default_wifi_sta();
        if (!staNetif) return false;
    }
    if (!apNetif) {
        // Try to get an already-existing AP netif (Arduino framework may have created it)
        apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (!apNetif) apNetif = esp_netif_create_default_wifi_ap();
        if (!apNetif) return false;
    }

    if (!wifiInitialized) {
        RAM_LOG("before-esp-wifi-init");
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
#if CONFIG_ESP_HOSTED_ENABLED
        // Required for ESP-Hosted remote Wi-Fi; matches Arduino's WiFiGeneric
        // initialization path and avoids stale slave-side persisted config.
        cfg.nvs_enable = false;
#endif
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { return false; }
        wifiInitialized = true;
        RAM_LOG("after-esp-wifi-init");
    }

    if (!handlersRegistered) {
        err = esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandler, nullptr, nullptr
        );
        if (err != ESP_OK) return false;
        err = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, wifiEventHandler, nullptr, nullptr
        );
        if (err != ESP_OK) return false;
        handlersRegistered = true;
    }

    if (!reconnectTimer) {
        esp_timer_create_args_t targs = {};
        targs.callback = &reconnectTimerCb;
        targs.name = "wifi_reconnect";
        esp_timer_create(&targs, &reconnectTimer);
    }

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    return true;
}

std::string ipFromNetif(esp_netif_t *netif) {
    if (!netif) return "";
    esp_netif_ip_info_t ip = {};
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK) return "";
    char out[16];
    snprintf(out, sizeof(out), IPSTR, IP2STR(&ip.ip));
    return out;
}

bool staHasIpAddress() {
    if (!staNetif) return false;
    esp_netif_ip_info_t ip = {};
    if (esp_netif_get_ip_info(staNetif, &ip) != ESP_OK) return false;
    return ip.ip.addr != 0;
}
} // namespace

bool launcherWifiStartSta() {
    if (!ensureWifiInitialized()) return false;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return false;
    esp_wifi_set_ps(WIFI_PS_NONE);
    err = esp_wifi_start();
    if (!okOrAlready(err)) return false;
    EventBits_t bits = xEventGroupWaitBits(
        wifiEvents, WIFI_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000)
    );
    if ((bits & WIFI_STARTED_BIT) == 0) return false;
    return true;
}

bool launcherWifiInitHostedSdio(
    int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t rst
) {
#if CONFIG_ESP_HOSTED_ENABLED
    if (!hostedSetPins(clk, cmd, d0, d1, d2, d3, rst)) return false;
    if (!hostedInitWiFi()) return false;
    return launcherWifiStartSta();
#else
    (void)clk;
    (void)cmd;
    (void)d0;
    (void)d1;
    (void)d2;
    (void)d3;
    (void)rst;
    return true;
#endif
}

LauncherWifiConnectState launcherWifiConnectStatus(
    const char *ssid, const char *password, uint32_t timeout_ms
) {
    if (!launcherWifiStartSta()) return LauncherWifiConnectState::Failed;

    // Only (re-)initiate if not already waiting for this SSID to connect.
    // The caller may call us in a retry loop; we must not interrupt an ongoing
    // WPA2 handshake (auth + 4-way handshake + DHCP can take 3-5 s).
    if (!expectingConnection) {
        // Serial.printf("[idf_wifi] Connecting to: %s\n", ssid ? ssid : "(null)");

        // Starting a fresh manual connect: cancel any pending background reconnect
        // so a stray DISCONNECTED from the disconnect below cannot re-arm it.
        autoReconnect = false;
        if (reconnectTimer) esp_timer_stop(reconnectTimer);

        wifi_config_t config = {};
        strlcpy(reinterpret_cast<char *>(config.sta.ssid), ssid ? ssid : "", sizeof(config.sta.ssid));
        strlcpy(
            reinterpret_cast<char *>(config.sta.password),
            password ? password : "",
            sizeof(config.sta.password)
        );
        config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

        // Disconnect first; expectingConnection is false so the DISCONNECTED
        // event won't set FAIL_BIT.
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
        lastDisconnectReason = WIFI_REASON_UNSPECIFIED;
        xEventGroupClearBits(wifiEvents, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

        esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
        if (err != ESP_OK) {
            // Serial.printf("[idf_wifi] set_config failed: %d\n", (int)err);
            return LauncherWifiConnectState::Failed;
        }
        wifiConnectRetryCount = 0;
        expectingConnection = true; // arm before connect so we catch DISCONNECTED
        err = esp_wifi_connect();
        if (err != ESP_OK) {
            // Serial.printf("[idf_wifi] esp_wifi_connect failed: %d\n", (int)err);
            expectingConnection = false;
            return LauncherWifiConnectState::Failed;
        }
    }
    // else { Serial.printf("[idf_wifi] Still waiting for connection...\n"); }

    EventBits_t bits = xEventGroupWaitBits(
        wifiEvents, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms)
    );
    if ((bits & WIFI_CONNECTED_BIT) != 0) return LauncherWifiConnectState::Connected;
    if ((bits & WIFI_FAIL_BIT) != 0) {
        if (isWrongPasswordReason(lastDisconnectReason)) return LauncherWifiConnectState::WrongPassword;
        return LauncherWifiConnectState::Failed;
    }
    return LauncherWifiConnectState::Pending;
}

bool launcherWifiConnect(const char *ssid, const char *password, uint32_t timeout_ms) {
    return launcherWifiConnectStatus(ssid, password, timeout_ms) == LauncherWifiConnectState::Connected;
}

int launcherWifiScan(std::vector<LauncherWifiAp> &out) {
    out.clear();
    if (!launcherWifiStartSta()) return -1;

    wifi_scan_config_t scanConfig = {};
    scanConfig.show_hidden = true;
    scanConfig.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scanConfig.scan_time.active.min = 100;
    scanConfig.scan_time.active.max = 300;
    esp_err_t err = esp_wifi_scan_start(&scanConfig, true);
    if (err != ESP_OK) return -1;

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (!count) return 0;

    std::vector<wifi_ap_record_t> records(count);
    if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) return -1;
    out.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        LauncherWifiAp ap;
        ap.ssid = reinterpret_cast<const char *>(records[i].ssid);
        ap.authmode = records[i].authmode;
        ap.rssi = records[i].rssi;
        out.push_back(ap);
    }
    return static_cast<int>(out.size());
}

bool launcherWifiStartAp(const char *ssid, const char *password, uint8_t channel, uint8_t max_clients) {
    if (!ensureWifiInitialized()) return false;

    esp_netif_ip_info_t ipInfo = {};
    IP4_ADDR(&ipInfo.ip, 172, 0, 0, 1);
    IP4_ADDR(&ipInfo.gw, 172, 0, 0, 1);
    IP4_ADDR(&ipInfo.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(apNetif);
    esp_netif_set_ip_info(apNetif, &ipInfo);
    esp_netif_dhcps_start(apNetif);

    wifi_config_t config = {};
    strlcpy(reinterpret_cast<char *>(config.ap.ssid), ssid ? ssid : "Launcher", sizeof(config.ap.ssid));
    config.ap.ssid_len = strlen(reinterpret_cast<const char *>(config.ap.ssid));
    strlcpy(
        reinterpret_cast<char *>(config.ap.password), password ? password : "", sizeof(config.ap.password)
    );
    config.ap.channel = channel;
    config.ap.max_connection = max_clients;
    config.ap.authmode =
        strlen(reinterpret_cast<const char *>(config.ap.password)) ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK) return false;
    return okOrAlready(esp_wifi_start());
}

bool launcherWifiStop() {
    if (!wifiInitialized) return true;
    expectingConnection = false;
    autoReconnect = false; // explicit stop: do not auto-reconnect in the background
    if (reconnectTimer) esp_timer_stop(reconnectTimer);
    wifiConnectRetryCount = 0;
    xEventGroupClearBits(wifiEvents, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) return false;
    return true;
}

bool launcherWifiIsConnected() {
    if (!wifiEvents) return false;
    if ((xEventGroupGetBits(wifiEvents) & WIFI_CONNECTED_BIT) == 0) return false;
    return staHasIpAddress();
}

std::string launcherWifiLocalIp() { return ipFromNetif(staNetif); }

std::string launcherWifiApIp() { return ipFromNetif(apNetif); }

std::string launcherWifiMac() {
    if (!ensureWifiInitialized()) return "";
    uint8_t mac[6] = {};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK) esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char out[18];
    snprintf(
        out, sizeof(out), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    );
    return out;
}

bool launcherMdnsStart(const char *host, uint16_t port) {
    if (mdnsStarted) mdns_free();
    if (mdns_init() != ESP_OK) return false;
    mdnsStarted = true;
    if (mdns_hostname_set(host) != ESP_OK) return false;
    mdns_instance_name_set(host);
    mdns_service_add(nullptr, "_http", "_tcp", port, nullptr, 0);
    return true;
}

void launcherMdnsStop() {
    if (!mdnsStarted) return;
    mdns_free();
    mdnsStarted = false;
}
