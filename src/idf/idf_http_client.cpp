#include "idf_http_client.h"

#include "ca_certs.h"
#include "esp_http_client.h"
#include <cstring>
#include <strings.h>

namespace {
constexpr int kTimeoutMs = 15000;
constexpr int kBufferSize = 1024;
constexpr int kMaxRedirects = 5;

esp_err_t httpEventHandler(esp_http_client_event_t *evt) {
    if (!evt || evt->event_id != HTTP_EVENT_ON_HEADER || !evt->user_data) return ESP_OK;

    LauncherHttpResponse *response = static_cast<LauncherHttpResponse *>(evt->user_data);
    if (evt->header_key && evt->header_value && strcasecmp(evt->header_key, "Content-Range") == 0) {
        strncpy(response->content_range, evt->header_value, sizeof(response->content_range) - 1);
        response->content_range[sizeof(response->content_range) - 1] = '\0';
    }
    return ESP_OK;
}

bool readResponse(esp_http_client_handle_t client, LauncherHttpChunkCb cb, void *ctx) {
    uint8_t buffer[kBufferSize];

    while (true) {
        int read = esp_http_client_read(client, reinterpret_cast<char *>(buffer), sizeof(buffer));
        if (read < 0) return false;
        if (read == 0) break;
        if (cb && !cb(buffer, read, ctx)) return false;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
}

bool executeGet(
    const char *url, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *response, const char *headerKey,
    const char *headerValue, const char *header2Key = nullptr, const char *header2Value = nullptr
) {
    LauncherHttpResponse localResponse;
    LauncherHttpResponse *resp = response ? response : &localResponse;
    *resp = LauncherHttpResponse();

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = kTimeoutMs;
    config.buffer_size = kBufferSize;
    config.buffer_size_tx = 512;
    config.max_redirection_count = kMaxRedirects;
    config.event_handler = httpEventHandler;
    config.user_data = resp;
    config.cert_pem = kRootCAs;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    auto applyHeaders = [&]() {
        if (headerKey && headerValue) esp_http_client_set_header(client, headerKey, headerValue);
        if (header2Key && header2Value) esp_http_client_set_header(client, header2Key, header2Value);
    };
    applyHeaders();

    // esp_http_client_open/fetch_headers does NOT auto-follow redirects unlike
    // esp_http_client_perform. Handle 3xx manually.
    bool ok = false;
    for (int redirects = 0; redirects <= kMaxRedirects; ++redirects) {
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) break;

        resp->content_length = esp_http_client_fetch_headers(client);
        resp->status = esp_http_client_get_status_code(client);

        if (resp->status >= 300 && resp->status < 400) {
            esp_http_client_close(client);
            if (esp_http_client_set_redirection(client) != ESP_OK) break;
            // Re-apply headers after redirect (may be cleared by URL change)
            applyHeaders();
            continue;
        }

        ok = resp->status >= 200 && resp->status < 300 && readResponse(client, cb, ctx);
        esp_http_client_close(client);
        break;
    }

    esp_http_client_cleanup(client);
    return ok;
}

struct StringSink {
    String *out;
    size_t maxSize;
};

bool stringChunkCb(const uint8_t *data, size_t len, void *ctx) {
    StringSink *sink = static_cast<StringSink *>(ctx);
    if (!sink || !sink->out) return false;
    if (sink->out->length() + len > sink->maxSize) return false;
    sink->out->concat(reinterpret_cast<const char *>(data), len);
    return true;
}
} // namespace

bool launcherHttpGetToString(const char *url, String &out, size_t maxSize) {
    out = "";
    StringSink sink = {&out, maxSize};
    LauncherHttpResponse response;
    return launcherHttpGetStream(url, stringChunkCb, &sink, &response) && response.status == 200;
}

bool launcherHttpGetStream(
    const char *url, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *response, const char *headerKey,
    const char *headerValue
) {
    return executeGet(url, cb, ctx, response, headerKey, headerValue);
}

bool launcherHttpGetRange(
    const char *url, uint32_t offset, uint32_t size, LauncherHttpChunkCb cb, void *ctx,
    LauncherHttpResponse *response, const char *hwid
) {
    String range = "bytes=" + String(offset) + "-" + String(offset + size - 1);
    return executeGet(url, cb, ctx, response, "Range", range.c_str(), "HWID", hwid);
}
