#include "idf_wifi.h"

#include "idf_wifi_at.h"
#include "ram_profile.h"

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_helpers.h"
#include <memory>
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

LauncherWifiBackend activeBackend = LauncherWifiBackend::Hosted;

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
        cfg.nvs_enable = false; // avoid flash writes in Launcher

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

extern "C" __attribute__((weak)) void launcherWifiResetSdioCoprocessor() {}

bool launcherWifiStartSta() {
    if (!ensureWifiInitialized()) return false;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return false;
    esp_wifi_set_ps(WIFI_PS_NONE);
    err = esp_wifi_start();
    if (!okOrAlready(err)) return false;
#ifdef LAUNCHER_WIFI_TX_POWER
    esp_wifi_set_max_tx_power(LAUNCHER_WIFI_TX_POWER);
#endif
    EventBits_t bits =
        xEventGroupWaitBits(wifiEvents, WIFI_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
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

bool hostedWifiAvailable = true;

namespace {
// NVS namespace/key holding the verdict from the last hosted bring-up attempt.
constexpr const char *kHostedNs = "launcher";
constexpr const char *kHostedKey = "hosted_st";

enum HostedGuardState : uint8_t {
    kHostedUntried = 0,
    kHostedAttempting = 1, // written just before the call, cleared right after
    kHostedGood = 2,
    kHostedBad = 3,
};

// How long to give the co-processor bring-up before giving up on it. A healthy
// one is dominated by CONFIG_ESP_HOSTED_SDIO_RESET_DELAY_MS (1500 ms) plus
// enumeration and handshake, so this is a wide margin. A broken one otherwise
// grinds for ~19 s and then panics: esp_hosted exposes no timeout knob and the
// IDF libs are pre-built, so the wait cannot be shortened from the inside - but
// we do not have to sit through it.
constexpr uint32_t kHostedInitTimeoutMs = 8000;

struct HostedInitCtx {
    int8_t clk, cmd, d0, d1, d2, d3, rst;
    volatile bool done;
    volatile bool result;
};

void hostedInitTask(void *arg) {
    auto *ctx = static_cast<HostedInitCtx *>(arg);
    ctx->result =
        launcherWifiInitHostedSdio(ctx->clk, ctx->cmd, ctx->d0, ctx->d1, ctx->d2, ctx->d3, ctx->rst);
    ctx->done = true;
    vTaskDelete(nullptr);
}

uint8_t hostedGuardRead() {
    lnvs::Handle handle(kHostedNs, false);
    if (!handle) return kHostedUntried;
    uint8_t state = kHostedUntried;
    if (nvs_get_u8(handle.raw(), kHostedKey, &state) != ESP_OK) return kHostedUntried;
    return state;
}

void hostedGuardWrite(uint8_t state) {
    lnvs::Handle handle(kHostedNs, true);
    if (!handle) return;
    if (nvs_set_u8(handle.raw(), kHostedKey, state) == ESP_OK) handle.commit();
}
} // namespace

void launcherWifiHostedResetGuard() { hostedGuardWrite(kHostedUntried); }

bool launcherWifiInitHostedSdioGuarded(
    int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t rst
) {
#if CONFIG_ESP_HOSTED_ENABLED
    // There is no cheap way to ask the co-processor "are you running ESP-Hosted?"
    // before talking to it - the handshake that would tell us is the very thing
    // that hangs. So instead of probing, we detect the damage after the fact: the
    // flag below survives the panic, and finding it still set means the previous
    // boot died inside the bring-up.
    const uint8_t state = hostedGuardRead();
    if (state == kHostedAttempting) {
        hostedGuardWrite(kHostedBad);
        hostedWifiAvailable = false;
        printf(
            "[hosted] previous bring-up crashed the device - Wi-Fi disabled.\n"
            "[hosted] flash the esp_hosted co-processor firmware, then run "
            "'wifi hosted retry'.\n"
        );
        return false;
    }
    if (state == kHostedBad) {
        hostedWifiAvailable = false;
        printf(
            "[hosted] co-processor marked unavailable - Wi-Fi disabled. "
            "Run 'wifi hosted retry' to probe again.\n"
        );
        return false;
    }

    hostedGuardWrite(kHostedAttempting);

    // Run the bring-up on its own task so a stuck co-processor cannot hold the
    // boot hostage. Static because the task keeps referencing it if we time out.
    static HostedInitCtx ctx;
    ctx = {clk, cmd, d0, d1, d2, d3, rst, false, false};
    // The hosted stack nests SDIO + RPC, so it needs a roomy stack.
    if (xTaskCreate(hostedInitTask, "hosted_init", 8192, &ctx, 5, nullptr) != pdPASS) {
        hostedGuardWrite(kHostedBad);
        hostedWifiAvailable = false;
        printf("[hosted] could not start bring-up task - Wi-Fi disabled.\n");
        return false;
    }

    const int64_t deadlineUs = esp_timer_get_time() + (int64_t)kHostedInitTimeoutMs * 1000;
    while (!ctx.done && esp_timer_get_time() < deadlineUs) { vTaskDelay(pdMS_TO_TICKS(20)); }

    if (!ctx.done) {
        // The task is wedged somewhere in SDIO and cannot be safely killed, and
        // letting it run on risks it panicking later at an arbitrary point. Bank
        // the verdict and restart now - deliberately, and roughly 10 s sooner
        // than the crash we would otherwise be waiting for.
        hostedGuardWrite(kHostedBad);
        printf(
            "[hosted] bring-up did not finish in %lu ms - marking co-processor "
            "unavailable and restarting.\n",
            (unsigned long)kHostedInitTimeoutMs
        );
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(50)); // let the message drain out of the console
        esp_restart();
    }

    const bool ok = ctx.result;
    hostedGuardWrite(ok ? kHostedGood : kHostedBad);
    hostedWifiAvailable = ok;
    if (!ok) printf("[hosted] co-processor bring-up failed - Wi-Fi disabled.\n");
    return ok;
#else
    return launcherWifiInitHostedSdio(clk, cmd, d0, d1, d2, d3, rst);
#endif
}

LauncherWifiBackend launcherWifiActiveBackend() { return activeBackend; }

bool launcherWifiInitSdioAuto(
    int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t rst
) {
    activeBackend = LauncherWifiBackend::Hosted;
#if defined(ENABLE_ESP_AT_INTERFACE)
    launcherWifiResetSdioCoprocessor();
    if (launcherWifiAtInit(clk, cmd, d0, d1, d2, d3)) {
        activeBackend = LauncherWifiBackend::EspAt;
        hostedWifiAvailable = true; // "Wi-Fi is available", regardless of transport
        return true;
    }
#endif

    activeBackend = LauncherWifiBackend::Hosted;
    launcherWifiResetSdioCoprocessor();
    if (launcherWifiInitHostedSdioGuarded(clk, cmd, d0, d1, d2, d3, rst)) return true;
    printf("[wifi] ESP-Hosted unavailable (tried ESP-AT first, also unavailable)\n");
    return false;
}

LauncherWifiConnectState
launcherWifiConnectStatus(const char *ssid, const char *password, uint32_t timeout_ms) {
#if defined(ENABLE_ESP_AT_INTERFACE)
    if (activeBackend == LauncherWifiBackend::EspAt) {
        return launcherWifiAtConnectStatus(ssid, password, timeout_ms);
    }
#endif
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
#if CONFIG_ESP_HOSTED_ENABLED
    // ESP-Hosted may update the STA netif before (or without) forwarding the
    // IP_EVENT_STA_GOT_IP event to the host.  In that case the connection is
    // already usable even though WIFI_CONNECTED_BIT was never set, and callers
    // would otherwise keep polling until their UI reports a false timeout.
    // Keep this fallback hosted-only so native Wi-Fi continues to use its
    // established event-driven result handling.
    if (staHasIpAddress()) {
        expectingConnection = false;
        wifiConnectRetryCount = 0;
        lastDisconnectReason = WIFI_REASON_UNSPECIFIED;
        autoReconnect = true;
        xEventGroupClearBits(wifiEvents, WIFI_FAIL_BIT);
        xEventGroupSetBits(wifiEvents, WIFI_CONNECTED_BIT);
        return LauncherWifiConnectState::Connected;
    }
#endif
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
#if defined(ENABLE_ESP_AT_INTERFACE)
    if (activeBackend == LauncherWifiBackend::EspAt) return launcherWifiAtScan(out);
#endif
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
#if defined(ENABLE_ESP_AT_INTERFACE)
    if (activeBackend == LauncherWifiBackend::EspAt) {
        printf("[wifi] AP mode not available: co-processor is running ESP-AT firmware\n");
        return false;
    }
#endif
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
#if defined(ENABLE_ESP_AT_INTERFACE)
    if (activeBackend == LauncherWifiBackend::EspAt) return launcherWifiAtDisconnect();
#endif
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
#if defined(ENABLE_ESP_AT_INTERFACE)
    if (activeBackend == LauncherWifiBackend::EspAt) return launcherWifiAtIsConnected();
#endif
    if (!wifiEvents) return false;
    if ((xEventGroupGetBits(wifiEvents) & WIFI_CONNECTED_BIT) == 0) return false;
    return staHasIpAddress();
}

std::string launcherWifiLocalIp() {
#if defined(ENABLE_ESP_AT_INTERFACE)
    if (activeBackend == LauncherWifiBackend::EspAt) return launcherWifiAtLocalIp();
#endif
    return ipFromNetif(staNetif);
}

std::string launcherWifiApIp() {
#if defined(ENABLE_ESP_AT_INTERFACE)
    if (activeBackend == LauncherWifiBackend::EspAt) return ""; // AP mode not supported over AT
#endif
    return ipFromNetif(apNetif);
}

std::string launcherWifiMac() {
#if defined(ENABLE_ESP_AT_INTERFACE)
    if (activeBackend == LauncherWifiBackend::EspAt) return launcherWifiAtMac();
#endif
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
#if defined(ENABLE_ESP_AT_INTERFACE)
    if (activeBackend == LauncherWifiBackend::EspAt) {
        printf("[wifi] mDNS not available: co-processor is running ESP-AT firmware\n");
        return false;
    }
#endif
    if (mdnsStarted) mdns_free();
    if (mdns_init() != ESP_OK) return false;
    mdnsStarted = true;
    if (mdns_hostname_set(host) != ESP_OK) return false;
    mdns_instance_name_set(host);
    mdns_service_add(nullptr, "_http", "_tcp", port, nullptr, 0);
    return true;
}

void launcherMdnsStop() {
#if defined(ENABLE_ESP_AT_INTERFACE)
    if (activeBackend == LauncherWifiBackend::EspAt) return;
#endif
    if (!mdnsStarted) return;
    mdns_free();
    mdnsStarted = false;
}
