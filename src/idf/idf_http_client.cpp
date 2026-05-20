#include "idf_http_client.h"

#include "ca_certs.h"
#include "esp_http_client.h"
#include "launcher_platform.h"
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace {
constexpr int kTimeoutMs = 15000;
constexpr int kHttpRxBufferSize = 2048;
constexpr int kHttpTxBufferSize = 1024;
constexpr int kMaxRedirects = 5;

struct HttpRequestContext {
    LauncherHttpResponse *response;
    LauncherHttpChunkCb cb;
    void *ctx;
    bool callbackOk;
};

esp_err_t httpEventHandler(esp_http_client_event_t *evt) {
    if (!evt || !evt->user_data) return ESP_OK;

    HttpRequestContext *request = static_cast<HttpRequestContext *>(evt->user_data);
    LauncherHttpResponse *response = request->response;
    if (evt->event_id == HTTP_EVENT_ON_HEADER && response) {
        int status = evt->client ? esp_http_client_get_status_code(evt->client) : 0;
        if (status >= 300 && status < 400) return ESP_OK;
        if (evt->header_key && evt->header_value && strcasecmp(evt->header_key, "Content-Length") == 0) {
            response->content_length = atoll(evt->header_value);
        } else if (
            evt->header_key && evt->header_value && strcasecmp(evt->header_key, "Content-Range") == 0
        ) {
            strncpy(response->content_range, evt->header_value, sizeof(response->content_range) - 1);
            response->content_range[sizeof(response->content_range) - 1] = '\0';
        }
    } else if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        int status = evt->client ? esp_http_client_get_status_code(evt->client) : 0;
        if (status >= 300 && status < 400) { return ESP_OK; }
        if (status > 0 && (status < 200 || status >= 300)) {
            request->callbackOk = false;
            return ESP_FAIL;
        }
        if (request->cb &&
            !request->cb(static_cast<const uint8_t *>(evt->data), evt->data_len, request->ctx)) {
            request->callbackOk = false;
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

bool executeGet(
    const char *url, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *response, const char *headerKey,
    const char *headerValue, const char *header2Key = nullptr, const char *header2Value = nullptr
) {
    LauncherHttpResponse localResponse;
    LauncherHttpResponse *resp = response ? response : &localResponse;
    *resp = LauncherHttpResponse();
    HttpRequestContext request = {resp, cb, ctx, true};

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = kTimeoutMs;
    config.buffer_size = kHttpRxBufferSize;
    config.buffer_size_tx = kHttpTxBufferSize;
    config.max_redirection_count = kMaxRedirects;
    config.event_handler = httpEventHandler;
    config.user_data = &request;
    config.cert_pem = kRootCAs;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    auto applyHeaders = [&]() {
        esp_http_client_set_header(client, "Accept-Encoding", "identity");
        if (headerKey && headerValue) esp_http_client_set_header(client, headerKey, headerValue);
        if (header2Key && header2Value) esp_http_client_set_header(client, header2Key, header2Value);
    };
    applyHeaders();

    esp_err_t err = esp_http_client_perform(client);
    int64_t contentLength = esp_http_client_get_content_length(client);
    if (contentLength >= 0) resp->content_length = contentLength;
    resp->status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    bool ok = err == ESP_OK && request.callbackOk && resp->status >= 200 && resp->status < 300;
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
