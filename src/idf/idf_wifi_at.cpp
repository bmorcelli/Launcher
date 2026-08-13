#include "idf_wifi_at.h"

#if defined(ENABLE_ESP_AT_INTERFACE)

#include "esp_serial_slave_link/essl.h"
#include "esp_serial_slave_link/essl_sdio.h"
#include "launcher_platform.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>

namespace {

// Must match CONFIG_AT_SDIO_BLOCK_SIZE on the co-processor (T-Display-P4/main/examples/
// esp-at/main/interface/sdio/Kconfig - default and what this firmware uses is 512).
constexpr size_t kRecvBufferSize = 512;

sdmmc_card_t g_card;
essl_handle_t g_essl = nullptr;
bool g_busUp = false;
bool g_connected = false;

// Brings the SDIO bus up (host + card) and then hands it to ESP-IDF's own
// esp_serial_slave_link (ESSL) component - the official host-side counterpart to the
// `sdio_slave` driver the co-processor's AT firmware uses
// (T-Display-P4/main/examples/esp-at/main/interface/sdio/at_sdio_task.c calls
// sdio_slave_initialize()/sdio_slave_recv()/sdio_slave_transmit(), the high-level IDF
// API - not raw SLC registers). ESSL does the CCCR bring-up and block-size negotiation
// itself, and - crucially - picks the right register/interrupt-bit layout per slave
// chip (ESSL_SDIO_DEF_ESP32 vs ESSL_SDIO_DEF_ESP32C6 differ). Hand-rolling that
// protocol against LilyGO's classic-ESP32-flavoured reference driver is what every
// earlier version of this file did, and it never worked on this ESP32-C6 hardware.
bool busBringUp(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3) {
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = (gpio_num_t)clk;
    slot.cmd = (gpio_num_t)cmd;
    slot.d0 = (gpio_num_t)d0;
    slot.d1 = (gpio_num_t)d1;
    slot.d2 = (gpio_num_t)d2;
    slot.d3 = (gpio_num_t)d3;
    slot.width = 4;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) {
        printf("[wifi-at] sdmmc_host_init failed: %s\n", esp_err_to_name(err));
        return false;
    }
    err = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot);
    if (err != ESP_OK) {
        printf("[wifi-at] sdmmc_host_init_slot failed: %s\n", esp_err_to_name(err));
        sdmmc_host_deinit();
        return false;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags |= SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_PROBING;

    memset(&g_card, 0, sizeof(g_card));
    bool cardUp = false;
    for (uint8_t i = 0; i < 15 && !cardUp; i++) {
        err = sdmmc_card_init(&host, &g_card);
        cardUp = err == ESP_OK;
        if (!cardUp) vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!cardUp) {
        printf("[wifi-at] sdmmc_card_init failed: %s\n", esp_err_to_name(err));
        sdmmc_host_deinit();
        return false;
    }
    printf("[wifi-at] SDIO card initialized, max_freq=%u kHz\n", host.max_freq_khz);

    essl_sdio_config_t esslConfig = {};
    esslConfig.card = &g_card;
    esslConfig.recv_buffer_size = kRecvBufferSize;
    err = essl_sdio_init_dev(&g_essl, &esslConfig);
    if (err != ESP_OK) {
        printf("[wifi-at] essl_sdio_init_dev failed: %s\n", esp_err_to_name(err));
        sdmmc_host_deinit();
        return false;
    }
    // A single essl_init() attempt has been seen to fail with either a CRC error or a
    // timeout on the CCCR IOR (function-ready) read, apparently at random - the same
    // physical SDIO wiring carries ESP-Hosted traffic fine at a much higher clock, so
    // this looks like a one-shot read that just needs retrying rather than a real
    // signal-integrity problem. esp_hosted's own driver surely retries internally;
    // essl_init() does not, so do it here instead.
    bool esslReady = false;
    for (uint8_t attempt = 0; attempt < 10 && !esslReady; attempt++) {
        err = essl_init(g_essl, 1000);
        if (err == ESP_OK) {
            esslReady = true;
            break;
        }
        printf("[wifi-at] essl_init attempt %u failed: %s\n", attempt + 1, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!esslReady) {
        essl_sdio_deinit_dev(g_essl);
        g_essl = nullptr;
        sdmmc_host_deinit();
        return false;
    }

    g_busUp = true;
    return true;
}

void busTeardown() {
    if (!g_busUp) return;
    if (g_essl) {
        essl_sdio_deinit_dev(g_essl);
        g_essl = nullptr;
    }
    sdmmc_host_deinit();
    g_busUp = false;
    g_connected = false;
}

// Drains one pending packet (if any) into `out`. Returns false only on a genuine SDIO
// error - "nothing pending right now" is not an error and returns true having done
// nothing, so callers can just keep polling.
bool drainOnePacket(std::string &out) {
    uint32_t rxSize = 0;
    if (essl_get_rx_data_size(g_essl, &rxSize, 0) != ESP_OK) return false;
    if (rxSize == 0) return true;
    if (rxSize > 65536) return true; // guard against a bogus/desynced length

    const size_t rxAlloc = ((std::max<size_t>(rxSize, kRecvBufferSize) + 3) / 4) * 4;
    auto *buf = static_cast<uint8_t *>(heap_caps_malloc(rxAlloc, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!buf) return false;
    size_t outLen = 0;
    esp_err_t err = essl_get_packet(g_essl, buf, rxAlloc, &outLen, 200);
    if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
        heap_caps_free(buf);
        return false;
    }
    if (outLen > rxAlloc) outLen = rxAlloc;
    out.append(reinterpret_cast<const char *>(buf), outLen);
    heap_caps_free(buf);
    return true;
}

bool sendPacket(const uint8_t *data, size_t len) {
    if (!data || !len) return false;
    return essl_send_packet(g_essl, data, len, 2000) == ESP_OK;
}

bool sendPacket(const std::string &data) {
    return sendPacket(reinterpret_cast<const uint8_t *>(data.data()), data.size());
}

// Polls the bus until `token` shows up in the accumulated response (true), `altToken`
// shows up instead (false), or timeoutMs elapses (false). Plain substring search, so
// only safe for text-only responses - see waitForHttpClientResponse() for the
// binary-safe variant HTTP bodies need.
bool waitForToken(const char *token, const char *altToken, uint32_t timeoutMs, std::string &response) {
    const uint32_t deadline = launcherMillis() + timeoutMs;
    uint8_t consecutiveErrors = 0;
    while ((int32_t)(launcherMillis() - deadline) < 0) {
        if (!drainOnePacket(response)) {
            if (++consecutiveErrors >= 50) return false;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        consecutiveErrors = 0;
        if (token && response.find(token) != std::string::npos) return true;
        if (altToken && response.find(altToken) != std::string::npos) return false;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

bool parseAtDataBlock(
    std::string &raw, const char *prefix, LauncherHttpChunkCb cb, void *ctx, size_t &totalBytes
) {
    const size_t marker = raw.find(prefix);
    if (marker == std::string::npos) {
        const size_t keep = strlen(prefix) + 16;
        if (raw.size() > keep) raw.erase(0, raw.size() - keep);
        return true;
    }
    if (marker > 0) raw.erase(0, marker);

    const size_t lenStart = strlen(prefix);
    size_t comma = raw.find(',', lenStart);
    if (comma == std::string::npos) return true;

    char *end = nullptr;
    long blockLen = strtol(raw.c_str() + lenStart, &end, 10);
    if (end != raw.c_str() + comma || blockLen < 0 || blockLen > 65536) {
        raw.erase(0, lenStart);
        return true;
    }

    const size_t bodyStart = comma + 1;
    const size_t bodyEnd = bodyStart + (size_t)blockLen;
    if (raw.size() < bodyEnd) return true;

    if (blockLen > 0 && cb && !cb(reinterpret_cast<const uint8_t *>(raw.data() + bodyStart), blockLen, ctx)) {
        return false;
    }
    totalBytes += (size_t)blockLen;
    raw.erase(0, bodyEnd);
    return true;
}

// HTTPCLIENT/HTTPCGET may split payloads into multiple `+HTTPCLIENT:<len>,<data>`
// records (commonly 512-byte chunks). Parse records one by one and only look for
// OK/ERROR in the text left between records, never inside a binary body.
int parseAtHttpError(const std::string &raw, const char *prefix) {
    std::string marker = std::string(prefix ? prefix : "") + "0x";
    size_t pos = raw.find(marker);
    if (pos == std::string::npos) return 0;
    pos += marker.size();
    char *end = nullptr;
    long value = strtol(raw.c_str() + pos, &end, 16);
    return end == raw.c_str() + pos ? 0 : (int)value;
}

int statusFromHttpClientError(int atError) {
    return atError >= 0x7000 && atError < 0x8000 ? atError - 0x7000 : 0;
}

bool waitForHttpDataResponse(
    const char *prefix, uint32_t timeoutMs, LauncherHttpChunkCb cb, void *ctx, size_t &totalBytes, bool &ok,
    int &atHttpError
) {
    const uint32_t deadline = launcherMillis() + timeoutMs;
    std::string raw;
    totalBytes = 0;
    ok = false;
    atHttpError = 0;
    uint8_t consecutiveErrors = 0;

    while ((int32_t)(launcherMillis() - deadline) < 0) {
        if (!drainOnePacket(raw)) {
            if (++consecutiveErrors >= 50) return false;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        consecutiveErrors = 0;

        while (raw.find(prefix) != std::string::npos) {
            const size_t before = raw.size();
            if (!parseAtDataBlock(raw, prefix, cb, ctx, totalBytes)) return false;
            if (raw.size() == before) break;
        }

        if (raw.find("\r\nOK\r\n") != std::string::npos) {
            ok = true;
            return true;
        }
        atHttpError = parseAtHttpError(raw, prefix);
        if (raw.find("\r\nERROR\r\n") != std::string::npos || atHttpError != 0) {
            ok = false;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    launcherConsolePrintf("[wifi-at] HTTP response timeout, received=%u bytes\n", (unsigned)totalBytes);
    return false;
}

// Escapes '\\', '"' and ',' the way AT string parameters require.
std::string atEscape(const char *s) {
    std::string out;
    if (!s) return out;
    for (const char *p = s; *p; ++p) {
        if (*p == '\\' || *p == '"' || *p == ',') out.push_back('\\');
        out.push_back(*p);
    }
    return out;
}

bool sendCommand(const std::string &cmd, std::string &response, uint32_t timeoutMs = 5000) {
    response.clear();
    if (!sendPacket(cmd + "\r\n")) return false;
    return waitForToken("\r\nOK\r\n", "\r\nERROR\r\n", timeoutMs, response);
}

int parseCwjapError(const std::string &response) {
    const size_t pos = response.find("+CWJAP:");
    if (pos == std::string::npos) return 0;
    return atoi(response.c_str() + pos + 7);
}

int transportTypeForUrl(const char *url) { return (url && strncmp(url, "https://", 8) == 0) ? 2 : 1; }

bool sendDataModeCommand(
    const std::string &cmd, const uint8_t *data, size_t len, const char *doneToken, uint32_t timeoutMs
) {
    std::string response;
    launcherConsolePrintf("[wifi-at] data command len=%u payload=%u\n", (unsigned)cmd.size(), (unsigned)len);
    if (!sendPacket(cmd + "\r\n")) return false;
    if (!waitForToken(">", "\r\nERROR\r\n", 5000, response)) {
        launcherConsolePrintf("[wifi-at] data command prompt failed: %s\n", response.c_str());
        return false;
    }
    if (len && !sendPacket(data, len)) return false;
    response.clear();
    const bool ok = waitForToken(doneToken, "\r\nERROR\r\n", timeoutMs, response);
    if (!ok) launcherConsolePrintf("[wifi-at] data command done failed: %s\n", response.c_str());
    return ok;
}

bool setLongHttpUrl(const char *url) {
    if (!url) return false;
    const size_t len = strlen(url);
    std::string cmd = "AT+HTTPURLCFG=" + std::to_string(len);
    return sendDataModeCommand(cmd, reinterpret_cast<const uint8_t *>(url), len, "SET OK", 10000);
}

bool clearHttpHeaders() {
    std::string response;
    return sendCommand("AT+HTTPCHEAD=0", response, 5000);
}

bool addHttpHeader(const std::string &header) {
    if (header.empty()) return true;
    std::string cmd = "AT+HTTPCHEAD=" + std::to_string(header.size());
    return sendDataModeCommand(
        cmd, reinterpret_cast<const uint8_t *>(header.data()), header.size(), "\r\nOK\r\n", 5000
    );
}

struct RangeLimitSink {
    LauncherHttpChunkCb cb;
    void *ctx;
    size_t limit;
    size_t delivered;
    bool overflow;
};

bool rangeLimitCb(const uint8_t *data, size_t len, void *ctx) {
    auto *sink = static_cast<RangeLimitSink *>(ctx);
    if (!sink) return false;
    const size_t remaining = sink->delivered < sink->limit ? sink->limit - sink->delivered : 0;
    const size_t passLen = std::min(len, remaining);
    if (passLen > 0 && sink->cb && !sink->cb(data, passLen, sink->ctx)) return false;
    sink->delivered += passLen;
    if (passLen < len) sink->overflow = true;
    return true;
}

struct BufferedRangeSink {
    std::vector<uint8_t> data;
    size_t limit;
    bool overflow;
};

bool bufferedRangeCb(const uint8_t *data, size_t len, void *ctx) {
    auto *sink = static_cast<BufferedRangeSink *>(ctx);
    if (!sink) return false;
    const size_t remaining = sink->data.size() < sink->limit ? sink->limit - sink->data.size() : 0;
    const size_t copyLen = std::min(len, remaining);
    if (copyLen > 0) sink->data.insert(sink->data.end(), data, data + copyLen);
    if (copyLen < len) sink->overflow = true;
    return true;
}

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string path;
    int port = 0;
    bool ssl = false;
};

struct RawHttpMeta {
    bool headersDone = false;
    bool callbackOk = true;
    int64_t bodyReceived = 0;
    std::string headerBuf;
    std::string location;
};

bool parseUrl(const char *url, ParsedUrl &out) {
    if (!url) return false;
    const char *schemeEnd = strstr(url, "://");
    if (!schemeEnd) return false;
    out.scheme.assign(url, schemeEnd - url);
    out.ssl = out.scheme == "https";
    if (!out.ssl && out.scheme != "http") return false;
    const char *hostStart = schemeEnd + 3;
    const char *pathStart = strchr(hostStart, '/');
    std::string hostPort = pathStart ? std::string(hostStart, pathStart - hostStart) : std::string(hostStart);
    out.path = pathStart ? pathStart : "/";
    size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        out.host = hostPort.substr(0, colon);
        out.port = atoi(hostPort.c_str() + colon + 1);
    } else {
        out.host = hostPort;
        out.port = out.ssl ? 443 : 80;
    }
    return !out.host.empty() && out.port > 0;
}

std::string trimAscii(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) start++;
    if (start) s.erase(0, start);
    return s;
}

bool headerStartsWith(const std::string &line, const char *prefix) {
    const size_t n = strlen(prefix);
    return line.size() >= n && strncasecmp(line.c_str(), prefix, n) == 0;
}

bool parseRawHttpHeaders(const std::string &headers, LauncherHttpResponse *resp, RawHttpMeta &meta) {
    size_t lineStart = 0;
    size_t lineEnd = headers.find("\r\n");
    if (lineEnd == std::string::npos) return false;
    std::string statusLine = headers.substr(0, lineEnd);
    size_t firstSpace = statusLine.find(' ');
    resp->status = firstSpace == std::string::npos ? 0 : atoi(statusLine.c_str() + firstSpace + 1);

    lineStart = lineEnd + 2;
    while (lineStart < headers.size()) {
        lineEnd = headers.find("\r\n", lineStart);
        if (lineEnd == std::string::npos || lineEnd == lineStart) break;
        std::string line = headers.substr(lineStart, lineEnd - lineStart);
        if (headerStartsWith(line, "Content-Length:")) {
            resp->content_length = atoll(trimAscii(line.substr(15)).c_str());
        } else if (headerStartsWith(line, "Content-Range:")) {
            std::string value = trimAscii(line.substr(14));
            strncpy(resp->content_range, value.c_str(), sizeof(resp->content_range) - 1);
            resp->content_range[sizeof(resp->content_range) - 1] = '\0';
        } else if (headerStartsWith(line, "Location:")) {
            meta.location = trimAscii(line.substr(9));
        }
        lineStart = lineEnd + 2;
    }
    return resp->status > 0;
}

std::string resolveLocation(const ParsedUrl &base, const std::string &location) {
    if (location.find("http://") == 0 || location.find("https://") == 0) return location;
    if (location.empty()) return location;
    if (location[0] == '/') return base.scheme + "://" + base.host + location;
    std::string prefix = base.path;
    size_t slash = prefix.rfind('/');
    prefix = slash == std::string::npos ? "/" : prefix.substr(0, slash + 1);
    return base.scheme + "://" + base.host + prefix + location;
}

bool parseCipRecvDataBlock(std::string &raw, std::vector<uint8_t> &out) {
    const char *prefix = "+CIPRECVDATA:";
    const size_t marker = raw.find(prefix);
    if (marker == std::string::npos) {
        const size_t keep = strlen(prefix) + 16;
        if (raw.size() > keep) raw.erase(0, raw.size() - keep);
        return true;
    }
    if (marker > 0) raw.erase(0, marker);

    const size_t lenStart = strlen(prefix);
    size_t comma = raw.find(',', lenStart);
    if (comma == std::string::npos) return true;
    char *end = nullptr;
    long blockLen = strtol(raw.c_str() + lenStart, &end, 10);
    if (end != raw.c_str() + comma || blockLen < 0 || blockLen > 8192) {
        raw.erase(0, lenStart);
        return true;
    }
    const size_t bodyStart = comma + 1;
    const size_t bodyEnd = bodyStart + static_cast<size_t>(blockLen);
    if (raw.size() < bodyEnd) return true;
    out.assign(
        reinterpret_cast<const uint8_t *>(raw.data() + bodyStart),
        reinterpret_cast<const uint8_t *>(raw.data() + bodyEnd)
    );
    raw.erase(0, bodyEnd);
    return true;
}

bool readCipRecvData(size_t len, std::vector<uint8_t> &out, uint32_t timeoutMs) {
    out.clear();
    std::string raw;
    std::string cmd = "AT+CIPRECVDATA=" + std::to_string(len);
    if (!sendPacket(cmd + "\r\n")) return false;
    const uint32_t deadline = launcherMillis() + timeoutMs;
    while ((int32_t)(launcherMillis() - deadline) < 0) {
        if (!drainOnePacket(raw)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (raw.find("+CIPRECVDATA:") != std::string::npos) {
            const size_t before = raw.size();
            if (!parseCipRecvDataBlock(raw, out)) return false;
            if (raw.size() != before || !out.empty()) {
                while (raw.find("\r\nOK\r\n") == std::string::npos &&
                       raw.find("\r\nERROR\r\n") == std::string::npos &&
                       (int32_t)(launcherMillis() - deadline) < 0) {
                    if (!drainOnePacket(raw)) vTaskDelay(pdMS_TO_TICKS(10));
                    else vTaskDelay(pdMS_TO_TICKS(2));
                }
                return raw.find("\r\nOK\r\n") != std::string::npos;
            }
        }
        if (raw.find("\r\nERROR\r\n") != std::string::npos) return false;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return false;
}

int queryCipRecvLen() {
    std::string response;
    if (!sendCommand("AT+CIPRECVLEN?", response, 2000)) return -1;
    size_t pos = response.find("+CIPRECVLEN:");
    if (pos == std::string::npos) return -1;
    pos += 12;
    return atoi(response.c_str() + pos);
}

int consumeIpdLength(std::string &raw) {
    size_t pos = raw.find("+IPD,");
    if (pos == std::string::npos) {
        const size_t keep = 16;
        if (raw.size() > keep) raw.erase(0, raw.size() - keep);
        return 0;
    }
    size_t lenStart = pos + 5;
    size_t lenEnd = lenStart;
    while (lenEnd < raw.size() && isdigit((unsigned char)raw[lenEnd])) lenEnd++;
    if (lenEnd == lenStart) return 0;
    int len = atoi(raw.c_str() + lenStart);
    size_t eraseEnd = lenEnd;
    while (eraseEnd < raw.size() &&
           (raw[eraseEnd] == '\r' || raw[eraseEnd] == '\n' || raw[eraseEnd] == ':')) {
        eraseEnd++;
    }
    raw.erase(0, eraseEnd);
    return len;
}

bool feedRawHttpBytes(
    const uint8_t *data, size_t len, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *resp,
    RawHttpMeta &meta
) {
    if (!meta.headersDone) {
        meta.headerBuf.append(reinterpret_cast<const char *>(data), len);
        size_t headerEnd = meta.headerBuf.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            if (meta.headerBuf.size() > 16384) return false;
            return true;
        }
        if (!parseRawHttpHeaders(meta.headerBuf.substr(0, headerEnd + 2), resp, meta)) return false;
        meta.headersDone = true;
        const size_t bodyStart = headerEnd + 4;
        const size_t bodyLen = meta.headerBuf.size() - bodyStart;
        if (bodyLen > 0 && resp->status >= 200 && resp->status < 300) {
            if (cb &&
                !cb(reinterpret_cast<const uint8_t *>(meta.headerBuf.data() + bodyStart), bodyLen, ctx)) {
                meta.callbackOk = false;
                return false;
            }
            meta.bodyReceived += bodyLen;
        }
        meta.headerBuf.clear();
        return true;
    }

    if (resp->status >= 200 && resp->status < 300) {
        if (len > 0 && cb && !cb(data, len, ctx)) {
            meta.callbackOk = false;
            return false;
        }
        meta.bodyReceived += len;
    }
    return true;
}

bool closeTcpConnection() {
    std::string response;
    return sendCommand("AT+CIPCLOSE", response, 3000);
}

bool rawHttpGetOnce(
    const char *url, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *resp, const char *headerKey,
    const char *headerValue, std::string &redirectTo
) {
    ParsedUrl parsed;
    redirectTo.clear();
    if (!parseUrl(url, parsed)) return false;

    std::string response;
    sendCommand("AT+CIPCLOSE", response, 1000);
    if (!sendCommand("AT+CIPMUX=0", response, 3000)) return false;
    if (!sendCommand("AT+CIPRECVTYPE=1", response, 3000)) return false;
    if (parsed.ssl) {
        sendCommand("AT+CIPSSLCCONF=0", response, 3000);
        if (parsed.host.size() <= 64) {
            sendCommand("AT+CIPSSLCSNI=\"" + atEscape(parsed.host.c_str()) + "\"", response, 3000);
        }
    }

    std::string type = parsed.ssl ? "SSL" : "TCP";
    std::string startCmd = "AT+CIPSTART=\"" + type + "\",\"" + atEscape(parsed.host.c_str()) + "\"," +
                           std::to_string(parsed.port) + ",,,30000";
    launcherConsolePrintf(
        "[wifi-at] RAW GET host=%s port=%d path_len=%u\n",
        parsed.host.c_str(),
        parsed.port,
        (unsigned)parsed.path.size()
    );
    if (!sendCommand(startCmd, response, 35000)) {
        std::string fallbackCmd = "AT+CIPSTART=\"" + type + "\",\"" + atEscape(parsed.host.c_str()) + "\"," +
                                  std::to_string(parsed.port);
        if (!sendCommand(fallbackCmd, response, 35000)) {
            launcherConsolePrintf("[wifi-at] CIPSTART failed: %s\n", response.c_str());
            resp->transport_error = -1;
            return false;
        }
    }

    std::string request = "GET " + parsed.path + " HTTP/1.1\r\nHost: " + parsed.host +
                          "\r\nUser-Agent: Launcher-ESP-AT\r\nAccept: */*\r\nAccept-Encoding: identity\r\n"
                          "Connection: close\r\n";
    if (headerKey && headerValue) request += std::string(headerKey) + ": " + headerValue + "\r\n";
    request += "\r\n";

    if (!sendDataModeCommand(
            "AT+CIPSEND=" + std::to_string(request.size()),
            reinterpret_cast<const uint8_t *>(request.data()),
            request.size(),
            "SEND OK",
            10000
        )) {
        closeTcpConnection();
        resp->transport_error = -1;
        return false;
    }

    RawHttpMeta meta;
    const uint32_t idleDeadlineMs = 45000;
    uint32_t lastProgress = launcherMillis();
    uint32_t lastLenPoll = 0;
    uint32_t lastForcedRecv = 0;
    uint32_t lastRecvErrorLog = 0;
    std::string unsolicited;
    while ((int32_t)(launcherMillis() - lastProgress - idleDeadlineMs) < 0) {
        if (meta.headersDone && resp->content_length >= 0 && meta.bodyReceived >= resp->content_length) break;
        if (meta.headersDone && resp->status >= 300 && resp->status < 400 && !meta.location.empty()) break;

        drainOnePacket(unsolicited);
        int available = consumeIpdLength(unsolicited);
        if (available <= 0 && launcherMillis() - lastLenPoll >= 500) {
            available = queryCipRecvLen();
            lastLenPoll = launcherMillis();
        }
        if (available <= 0 && parsed.ssl && launcherMillis() - lastForcedRecv >= 250) {
            available = 2048;
            lastForcedRecv = launcherMillis();
        }
        if (available <= 0) {
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }
        const size_t readLen = std::min<size_t>(available, 2048);
        std::vector<uint8_t> chunk;
        if (!readCipRecvData(readLen, chunk, 5000)) {
            if (launcherMillis() - lastRecvErrorLog >= 2000) {
                launcherConsolePrintf(
                    "[wifi-at] CIPRECVDATA failed requested=%u available=%d\n", (unsigned)readLen, available
                );
                lastRecvErrorLog = launcherMillis();
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (chunk.empty()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        lastProgress = launcherMillis();
        if (!feedRawHttpBytes(chunk.data(), chunk.size(), cb, ctx, resp, meta)) {
            closeTcpConnection();
            resp->transport_error = meta.callbackOk ? -1 : 0;
            return false;
        }
    }

    closeTcpConnection();
    if (!meta.headersDone) {
        resp->transport_error = -1;
        return false;
    }
    if (resp->status >= 300 && resp->status < 400 && !meta.location.empty()) {
        redirectTo = resolveLocation(parsed, meta.location);
        return false;
    }
    if (!(resp->status >= 200 && resp->status < 300)) return false;
    if (resp->content_length >= 0 && meta.bodyReceived != resp->content_length) {
        launcherConsolePrintf(
            "[wifi-at] RAW GET incomplete status=%d got=%lld expected=%lld\n",
            resp->status,
            (long long)meta.bodyReceived,
            (long long)resp->content_length
        );
        resp->transport_error = -1;
        return false;
    }
    resp->content_length = meta.bodyReceived;
    return meta.callbackOk;
}

std::string httpUrlArg(const char *url, bool &usedLongUrl) {
    usedLongUrl = url && strlen(url) > 220;
    if (usedLongUrl) {
        launcherConsolePrintf("[wifi-at] using HTTPURLCFG url_len=%u\n", (unsigned)strlen(url));
        if (!setLongHttpUrl(url)) {
            launcherConsolePrintln("[wifi-at] HTTPURLCFG failed, falling back to inline URL");
            usedLongUrl = false;
        }
    }
    return usedLongUrl ? std::string() : atEscape(url);
}

std::string makeHttpClientCommand(
    int method, int contentType, const char *url, const char *data, const std::vector<std::string> &headers
) {
    bool usedLongUrl = false;
    std::string urlArg = httpUrlArg(url, usedLongUrl);
    std::string cmd = "AT+HTTPCLIENT=" + std::to_string(method) + "," + std::to_string(contentType) + ",\"" +
                      urlArg + "\",\"\",\"\"," + std::to_string(transportTypeForUrl(url));
    if (method == 3) cmd += ",\"" + atEscape(data ? data : "") + "\"";
    for (const auto &header : headers) cmd += ",\"" + atEscape(header.c_str()) + "\"";
    return cmd;
}

bool atHttpGetSize(const char *url, int64_t &sizeOut) {
    sizeOut = -1;
    if (!url) return false;
    std::string response;
    bool usedLongUrl = false;
    std::string urlArg = httpUrlArg(url, usedLongUrl);
    std::string cmd = "AT+HTTPGETSIZE=\"" + urlArg + "\",2048,2048,30000";
    launcherConsolePrintf("[wifi-at] HTTPGETSIZE cmd_len=%u\n", (unsigned)cmd.size());
    if (!sendCommand(cmd, response, 35000)) return false;
    size_t pos = response.find("+HTTPGETSIZE:");
    if (pos == std::string::npos) return false;
    pos += 13;
    sizeOut = strtoll(response.c_str() + pos, nullptr, 10);
    return sizeOut >= 0;
}

bool atHttpGetSizeWithHeaders(const char *url, const std::vector<std::string> &headers, int64_t &sizeOut) {
    clearHttpHeaders();
    bool headersOk = true;
    for (const auto &header : headers) headersOk = addHttpHeader(header) && headersOk;
    bool ok = headersOk && atHttpGetSize(url, sizeOut);
    clearHttpHeaders();
    return ok;
}

} // namespace

LauncherC6Firmware launcherWifiAtProbe(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3) {
    if (!busBringUp(clk, cmd, d0, d1, d2, d3)) return LauncherC6Firmware::kNoResponse;

    LauncherC6Firmware result = LauncherC6Firmware::kOther;
    std::string response;
    if (waitForToken("ready", nullptr, 1500, response)) result = LauncherC6Firmware::kEspAt;

    busTeardown();
    return result;
}

bool launcherWifiAtInit(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3) {
    if (g_busUp) busTeardown();
    if (!busBringUp(clk, cmd, d0, d1, d2, d3)) return false;

    std::string response;

    // The firmware announces itself unsolicited right after reset - consume that
    // banner here rather than mistaking it for a command's response.
    if (!waitForToken("ready", nullptr, 5000, response)) {
        launcherConsolePrintln("[wifi-at] no ready banner from co-processor");
        busTeardown();
        return false;
    }

    // Echo would otherwise show up inside every subsequent response and complicate
    // parsing - turn it off first, before anything else.
    if (!sendCommand("ATE0", response)) {
        launcherConsolePrintln("[wifi-at] ATE0 failed");
        busTeardown();
        return false;
    }
    if (!sendCommand("AT+CWMODE=1", response)) {
        launcherConsolePrintln("[wifi-at] AT+CWMODE=1 failed");
        busTeardown();
        return false;
    }
    // Single-connection mode: nothing here ever needs more than one HTTP request in
    // flight, and it keeps future +IPD-style parsing simpler if that's ever added.
    sendCommand("AT+CIPMUX=0", response);

    g_connected = false;
    launcherConsolePrintln("[wifi-at] co-processor ready");
    return true;
}

LauncherWifiConnectState
launcherWifiAtConnectStatus(const char *ssid, const char *password, uint32_t timeout_ms) {
    if (!g_busUp || !ssid) return LauncherWifiConnectState::Failed;
    std::string cmd = "AT+CWJAP=\"" + atEscape(ssid) + "\",\"" + atEscape(password ? password : "") + "\"";
    std::string response;
    const uint32_t joinTimeoutMs = std::max<uint32_t>(timeout_ms, 30000);
    g_connected = sendCommand(cmd, response, joinTimeoutMs);
    if (!g_connected) printf("[wifi-at] CWJAP failed, response: %s\n", response.c_str());
    else printf("[wifi-at] CWJAP connected\n");
    if (g_connected) return LauncherWifiConnectState::Connected;

    // ESP-AT reports join failures as +CWJAP:<code>; 2 is password/auth failure.
    if (parseCwjapError(response) == 2) return LauncherWifiConnectState::WrongPassword;
    return LauncherWifiConnectState::Failed;
}

bool launcherWifiAtConnect(const char *ssid, const char *password, uint32_t timeout_ms) {
    LauncherWifiConnectState state = launcherWifiAtConnectStatus(ssid, password, timeout_ms);
    g_connected = state == LauncherWifiConnectState::Connected;
    return g_connected;
}

bool launcherWifiAtDisconnect() {
    if (!g_busUp) return false;
    std::string response;
    bool ok = sendCommand("AT+CWQAP", response, 3000);
    g_connected = false;
    return ok;
}

int launcherWifiAtScan(std::vector<LauncherWifiAp> &out) {
    out.clear();
    if (!g_busUp) return -1;
    std::string response;
    if (!sendCommand("AT+CWLAP", response, 10000)) return -1;

    size_t pos = 0;
    while ((pos = response.find("+CWLAP:(", pos)) != std::string::npos) {
        pos += 8;
        size_t lineEnd = response.find(')', pos);
        if (lineEnd == std::string::npos) break;
        std::string entry = response.substr(pos, lineEnd - pos);
        pos = lineEnd + 1;

        // entry is: <ecn>,"<ssid>",<rssi>,...
        size_t c1 = entry.find(',');
        if (c1 == std::string::npos) continue;
        int ecn = atoi(entry.substr(0, c1).c_str());

        size_t q1 = entry.find('"', c1);
        if (q1 == std::string::npos) continue;
        size_t q2 = q1 + 1;
        std::string ssid;
        while (q2 < entry.size() && entry[q2] != '"') {
            if (entry[q2] == '\\' && q2 + 1 < entry.size()) {
                ssid.push_back(entry[q2 + 1]);
                q2 += 2;
                continue;
            }
            ssid.push_back(entry[q2]);
            q2++;
        }
        size_t c2 = entry.find(',', q2);
        int rssi = (c2 != std::string::npos) ? atoi(entry.c_str() + c2 + 1) : 0;

        LauncherWifiAp ap;
        ap.ssid = ssid;
        // ESP-AT's <ecn> enum was modeled after (and lines up numerically with)
        // ESP-IDF's wifi_auth_mode_t.
        ap.authmode = static_cast<wifi_auth_mode_t>(ecn);
        ap.rssi = static_cast<int8_t>(rssi);
        out.push_back(ap);
    }
    return static_cast<int>(out.size());
}

bool launcherWifiAtIsConnected() { return g_busUp && g_connected; }

std::string launcherWifiAtLocalIp() {
    if (!g_busUp) return "";
    std::string response;
    if (!sendCommand("AT+CIPSTA?", response, 3000)) return "";
    size_t pos = response.find("ip:\"");
    if (pos == std::string::npos) return "";
    pos += 4;
    size_t end = response.find('"', pos);
    if (end == std::string::npos) return "";
    return response.substr(pos, end - pos);
}

std::string launcherWifiAtMac() {
    if (!g_busUp) return "";
    std::string response;
    if (!sendCommand("AT+CIPSTAMAC?", response, 3000)) return "";
    size_t pos = response.find("+CIPSTAMAC:\"");
    if (pos == std::string::npos) return "";
    pos += 12;
    size_t end = response.find('"', pos);
    if (end == std::string::npos) return "";
    std::string mac = response.substr(pos, end - pos);
    for (char &c : mac) c = (char)toupper((unsigned char)c);
    return mac;
}

bool launcherWifiAtHttpGet(
    const char *url, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *response, uint32_t rangeOffset,
    uint32_t rangeSize, const char *headerKey, const char *headerValue
) {
    LauncherHttpResponse localResp;
    LauncherHttpResponse *resp = response ? response : &localResp;
    *resp = LauncherHttpResponse();
    if (!g_busUp || !url) return false;
    launcherConsolePrintf(
        "[wifi-at] HTTP GET url_len=%u range=%u\n", (unsigned)strlen(url), (unsigned)rangeSize
    );

    std::vector<std::string> headers;
    headers.push_back("Accept-Encoding: identity");
    if (headerKey && headerValue) headers.push_back(std::string(headerKey) + ": " + headerValue);
    int64_t totalSize = -1;
    if (rangeSize != 0) {
        if (rangeSize <= 4096) atHttpGetSizeWithHeaders(url, headers, totalSize);

        char range[96];
        snprintf(
            range,
            sizeof(range),
            "Range: bytes=%lu-%lu",
            (unsigned long)rangeOffset,
            (unsigned long)(rangeOffset + rangeSize - 1)
        );
        headers.push_back(range);

        clearHttpHeaders();
        bool headersOk = true;
        for (const auto &header : headers) headersOk = addHttpHeader(header) && headersOk;
        if (!headersOk) {
            clearHttpHeaders();
            resp->transport_error = -1;
            return false;
        }

        bool usedLongUrl = false;
        std::string urlArg = httpUrlArg(url, usedLongUrl);
        std::string getCmd = "AT+HTTPCGET=\"" + urlArg + "\",2048,2048,60000";
        launcherConsolePrintf("[wifi-at] HTTPCGET cmd_len=%u\n", (unsigned)getCmd.size());
        if (!sendPacket(getCmd + "\r\n")) {
            clearHttpHeaders();
            launcherConsolePrintln("[wifi-at] HTTPCGET send failed");
            resp->transport_error = -1;
            return false;
        }

        BufferedRangeSink buffered = {{}, rangeSize, false};
        buffered.data.reserve(std::min<size_t>(rangeSize, 16 * 1024));
        size_t bodyLen = 0;
        bool ok = false;
        int atHttpError = 0;
        const bool completed = waitForHttpDataResponse(
            "+HTTPCGET:", 90000, bufferedRangeCb, &buffered, bodyLen, ok, atHttpError
        );
        clearHttpHeaders();
        if (!completed) {
            resp->status = 0;
            resp->transport_error = -1;
            return false;
        }
        if (!ok) {
            resp->status = statusFromHttpClientError(atHttpError);
            resp->transport_error = atHttpError;
            launcherConsolePrintf(
                "[wifi-at] HTTPCGET failed at=0x%04X status=%d\n", atHttpError, resp->status
            );
            return false;
        }

        if (bodyLen > rangeSize || buffered.overflow) {
            resp->status = strstr(url, "/download?") != nullptr ? 302 : 200;
            resp->content_length = bodyLen;
            launcherConsolePrintf(
                "[wifi-at] HTTPCGET range ignored requested=%u received=%u\n",
                (unsigned)rangeSize,
                (unsigned)bodyLen
            );
            return false;
        }

        if (!buffered.data.empty() && cb && !cb(buffered.data.data(), buffered.data.size(), ctx)) {
            resp->transport_error = -1;
            return false;
        }
        resp->status = 206;
        resp->content_length = bodyLen;
        const unsigned long end = bodyLen == 0 ? rangeOffset : rangeOffset + (unsigned long)bodyLen - 1;
        snprintf(
            resp->content_range,
            sizeof(resp->content_range),
            "bytes %lu-%lu/%lld",
            (unsigned long)rangeOffset,
            end,
            (long long)(totalSize >= 0 ? totalSize : (int64_t)(end + 1))
        );
        return true;
    }

    std::string cmd = makeHttpClientCommand(2, 0, url, nullptr, headers);
    launcherConsolePrintf("[wifi-at] HTTPCLIENT GET cmd_len=%u\n", (unsigned)cmd.size());
    if (!sendPacket(cmd + "\r\n")) {
        launcherConsolePrintln("[wifi-at] HTTPCLIENT GET send failed");
        resp->transport_error = -1;
        return false;
    }

    size_t bodyLen = 0;
    bool ok = false;
    int atHttpError = 0;
    if (!waitForHttpDataResponse("+HTTPCLIENT:", 60000, cb, ctx, bodyLen, ok, atHttpError)) {
        resp->status = 0;
        resp->transport_error = -1;
        return false;
    }

    if (!ok) {
        resp->status = statusFromHttpClientError(atHttpError);
        resp->transport_error = atHttpError;
        launcherConsolePrintf(
            "[wifi-at] HTTPCLIENT GET failed at=0x%04X status=%d\n", atHttpError, resp->status
        );
        return false;
    }
    resp->status = 200;
    resp->content_length = bodyLen;
    return true;
}

bool launcherWifiAtHttpGetStreamRaw(
    const char *url, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *response, const char *headerKey,
    const char *headerValue
) {
    LauncherHttpResponse localResp;
    LauncherHttpResponse *resp = response ? response : &localResp;
    *resp = LauncherHttpResponse();
    if (!g_busUp || !url) return false;

    std::string currentUrl = url;
    for (uint8_t redirects = 0; redirects < 4; redirects++) {
        *resp = LauncherHttpResponse();
        std::string redirectTo;
        const bool ok = rawHttpGetOnce(currentUrl.c_str(), cb, ctx, resp, headerKey, headerValue, redirectTo);
        launcherConsolePrintf(
            "[wifi-at] RAW GET done ok=%d status=%d bytes=%lld redirect=%u\n",
            (int)ok,
            resp->status,
            (long long)resp->content_length,
            (unsigned)redirectTo.size()
        );
        if (ok) return true;
        if (!redirectTo.empty() && currentUrl.find("/download?") != std::string::npos) {
            size_t pos = currentUrl.find("/download?");
            currentUrl.replace(pos, 10, "/proxy?");
            launcherConsolePrintln("[wifi-at] RAW GET /download redirected, streaming via /proxy");
            continue;
        }
        if (!redirectTo.empty()) {
            currentUrl = redirectTo;
            continue;
        }
        // /download is the preferred API route, but /proxy remains a same-origin
        // fallback for ESP-AT firmwares or servers that refuse to expose/follow the
        // redirect on the co-processor side.
        if (redirects == 0 && currentUrl.find("/download?") != std::string::npos) {
            size_t pos = currentUrl.find("/download?");
            currentUrl.replace(pos, 10, "/proxy?");
            launcherConsolePrintln("[wifi-at] RAW GET falling back to /proxy");
            continue;
        }
        if (resp->status == 0 && resp->transport_error == 0) resp->transport_error = -1;
        return false;
    }
    resp->transport_error = -1;
    return false;
}

bool launcherWifiAtHttpPost(
    const char *url, const char *body, size_t bodyLen, String &out, size_t maxSize,
    LauncherHttpResponse *response
) {
    out = "";
    LauncherHttpResponse localResp;
    LauncherHttpResponse *resp = response ? response : &localResp;
    *resp = LauncherHttpResponse();
    if (!g_busUp || !url) return false;
    launcherConsolePrintf(
        "[wifi-at] HTTP POST url_len=%u body_len=%u\n", (unsigned)strlen(url), (unsigned)bodyLen
    );

    // Assumes a NUL-free text body (JSON, form data) - every existing caller of
    // launcherHttpPost() sends exactly that. A binary POST body would need the AT
    // string param built without relying on a C-string here.
    std::string data(body ? body : "", bodyLen);
    std::vector<std::string> headers;
    headers.push_back("Accept-Encoding: identity");
    std::string cmd = makeHttpClientCommand(3, 1, url, data.c_str(), headers);
    launcherConsolePrintf("[wifi-at] HTTPCLIENT POST cmd_len=%u\n", (unsigned)cmd.size());

    if (!sendPacket(cmd + "\r\n")) {
        launcherConsolePrintln("[wifi-at] HTTPCLIENT POST send failed");
        resp->transport_error = -1;
        return false;
    }

    struct PostSink {
        String *out;
        size_t maxSize;
        bool ok;
    } sink = {&out, maxSize, true};
    auto postCb = [](const uint8_t *data, size_t len, void *ctx) -> bool {
        auto *sink = static_cast<PostSink *>(ctx);
        if (!sink || !sink->out) return false;
        if (sink->out->length() + len > sink->maxSize) {
            sink->ok = false;
            return false;
        }
        sink->out->concat(reinterpret_cast<const char *>(data), len);
        return true;
    };

    size_t respLen = 0;
    bool ok = false;
    int atHttpError = 0;
    if (!waitForHttpDataResponse("+HTTPCLIENT:", 60000, postCb, &sink, respLen, ok, atHttpError)) {
        resp->status = 0;
        resp->transport_error = -1;
        return false;
    }

    resp->status = ok ? 200 : statusFromHttpClientError(atHttpError);
    resp->transport_error = ok ? 0 : atHttpError;
    if (!ok) {
        launcherConsolePrintf(
            "[wifi-at] HTTPCLIENT POST failed at=0x%04X status=%d\n", atHttpError, resp->status
        );
    }
    resp->content_length = respLen;
    if (!ok) return false;
    return sink.ok;
}

#endif // ENABLE_ESP_AT_INTERFACE
