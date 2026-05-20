#ifndef LAUNCHER_IDF_HTTP_CLIENT_H
#define LAUNCHER_IDF_HTTP_CLIENT_H

#include <WString.h>
#include <cstddef>
#include <cstdint>

struct LauncherHttpResponse {
    int status = 0;
    int64_t content_length = -1;
    char content_range[96] = {0};
};

using LauncherHttpChunkCb = bool (*)(const uint8_t *data, size_t len, void *ctx);

bool launcherHttpGetToString(const char *url, String &out, size_t maxSize = 65536);
bool launcherHttpGetStream(
    const char *url, LauncherHttpChunkCb cb, void *ctx, LauncherHttpResponse *response = nullptr,
    const char *headerKey = nullptr, const char *headerValue = nullptr
);
bool launcherHttpGetRange(
    const char *url, uint32_t offset, uint32_t size, LauncherHttpChunkCb cb, void *ctx,
    LauncherHttpResponse *response = nullptr, const char *hwid = nullptr
);

#endif
