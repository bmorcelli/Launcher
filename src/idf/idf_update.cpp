#include "idf_update.h"

#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr size_t kSectorSize = 4096;
constexpr size_t kAppHeaderHoldSize = 16;

struct LauncherUpdateContext {
    const esp_partition_t *partition = nullptr;
    LauncherUpdateTarget target = LAUNCHER_UPDATE_APP;
    size_t size = 0;
    size_t written = 0;
    int error = LAUNCHER_UPDATE_ERROR_OK;
    bool running = false;
    bool app_header_pending = false;
    uint8_t app_header[kAppHeaderHoldSize] = {0};
    size_t app_header_len = 0;
};

LauncherUpdateContext ctx;

size_t roundUpToSector(size_t value) {
    return (value + kSectorSize - 1) & ~(kSectorSize - 1);
}

void setError(int error) {
    ctx.error = error;
    ctx.running = false;
}

const esp_partition_t *findPartition(LauncherUpdateTarget target) {
    switch (target) {
        case LAUNCHER_UPDATE_APP: return esp_ota_get_next_update_partition(nullptr);
        case LAUNCHER_UPDATE_SPIFFS:
            return esp_partition_find_first(
                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr
            );
        case LAUNCHER_UPDATE_FAT_VFS:
            return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "vfs");
        case LAUNCHER_UPDATE_FAT_SYS:
            return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "sys");
    }
    return nullptr;
}

bool isBootableAppPartition(const esp_partition_t *partition) {
    uint8_t byte = 0;
    return partition && esp_partition_read(partition, 0, &byte, 1) == ESP_OK &&
           byte == ESP_IMAGE_HEADER_MAGIC;
}

bool writeAppDataWithDeferredHeader(const uint8_t *data, size_t len) {
    size_t offset = 0;

    if (ctx.app_header_pending && ctx.app_header_len < kAppHeaderHoldSize) {
        const size_t copy_len = std::min(kAppHeaderHoldSize - ctx.app_header_len, len);
        memcpy(ctx.app_header + ctx.app_header_len, data, copy_len);
        ctx.app_header_len += copy_len;
        ctx.written += copy_len;
        offset += copy_len;

        if (ctx.app_header_len == 1 && ctx.app_header[0] != ESP_IMAGE_HEADER_MAGIC) {
            setError(LAUNCHER_UPDATE_ERROR_MAGIC_BYTE);
            return false;
        }

        if (ctx.app_header_len < kAppHeaderHoldSize) return true;
    }

    if (offset >= len) return true;

    const size_t partition_offset = ctx.written;
    const size_t write_len = len - offset;
    esp_err_t err = esp_partition_write(ctx.partition, partition_offset, data + offset, write_len);
    if (err != ESP_OK) {
        setError(LAUNCHER_UPDATE_ERROR_WRITE);
        return false;
    }

    ctx.written += write_len;
    return true;
}

bool writeData(const uint8_t *data, size_t len) {
    if (ctx.target == LAUNCHER_UPDATE_APP) return writeAppDataWithDeferredHeader(data, len);

    esp_err_t err = esp_partition_write(ctx.partition, ctx.written, data, len);
    if (err != ESP_OK) {
        setError(LAUNCHER_UPDATE_ERROR_WRITE);
        return false;
    }
    ctx.written += len;
    return true;
}
}

bool launcherUpdateBegin(LauncherUpdateTarget target, size_t size) {
    ctx = LauncherUpdateContext();
    ctx.target = target;
    ctx.size = size;

    if (size == 0) {
        setError(LAUNCHER_UPDATE_ERROR_SIZE);
        return false;
    }

    ctx.partition = findPartition(target);
    if (!ctx.partition) {
        setError(LAUNCHER_UPDATE_ERROR_NO_PARTITION);
        return false;
    }

    if (size > ctx.partition->size) {
        setError(LAUNCHER_UPDATE_ERROR_SIZE);
        return false;
    }

    esp_err_t err = esp_partition_erase_range(ctx.partition, 0, roundUpToSector(size));
    if (err != ESP_OK) {
        setError(LAUNCHER_UPDATE_ERROR_ERASE);
        return false;
    }

    ctx.running = true;
    ctx.error = LAUNCHER_UPDATE_ERROR_OK;
    ctx.app_header_pending = target == LAUNCHER_UPDATE_APP;
    return true;
}

size_t launcherUpdateWrite(const uint8_t *data, size_t len) {
    if (!ctx.running || ctx.error != LAUNCHER_UPDATE_ERROR_OK || !data || len == 0) return 0;

    if (ctx.written + len > ctx.size) {
        setError(LAUNCHER_UPDATE_ERROR_SPACE);
        return 0;
    }

    if (!writeData(data, len)) return 0;
    return len;
}

bool launcherUpdateEnd() {
    if (!ctx.running || ctx.error != LAUNCHER_UPDATE_ERROR_OK) return false;
    if (ctx.written != ctx.size) {
        setError(LAUNCHER_UPDATE_ERROR_ABORT);
        return false;
    }

    if (ctx.target == LAUNCHER_UPDATE_APP) {
        if (ctx.app_header_len != kAppHeaderHoldSize) {
            setError(LAUNCHER_UPDATE_ERROR_MAGIC_BYTE);
            return false;
        }
        esp_err_t err = esp_partition_write(ctx.partition, 0, ctx.app_header, kAppHeaderHoldSize);
        if (err != ESP_OK) {
            setError(LAUNCHER_UPDATE_ERROR_WRITE);
            return false;
        }
        if (!isBootableAppPartition(ctx.partition)) {
            setError(LAUNCHER_UPDATE_ERROR_READ);
            return false;
        }

        err = esp_ota_set_boot_partition(ctx.partition);
        if (err != ESP_OK) {
            setError(LAUNCHER_UPDATE_ERROR_ACTIVATE);
            return false;
        }
    }

    ctx.running = false;
    return true;
}

void launcherUpdateAbort() {
    setError(LAUNCHER_UPDATE_ERROR_ABORT);
}

bool launcherUpdateIsFinished() {
    return ctx.size > 0 && ctx.written == ctx.size && ctx.error == LAUNCHER_UPDATE_ERROR_OK;
}

int launcherUpdateLastError() {
    return ctx.error;
}

const char *launcherUpdateLastErrorName() {
    switch (ctx.error) {
        case LAUNCHER_UPDATE_ERROR_OK: return "No Error";
        case LAUNCHER_UPDATE_ERROR_WRITE: return "Flash Write Failed";
        case LAUNCHER_UPDATE_ERROR_ERASE: return "Flash Erase Failed";
        case LAUNCHER_UPDATE_ERROR_READ: return "Flash Read Failed";
        case LAUNCHER_UPDATE_ERROR_SPACE: return "Not Enough Space";
        case LAUNCHER_UPDATE_ERROR_SIZE: return "Bad Size Given";
        case LAUNCHER_UPDATE_ERROR_STREAM: return "Stream Read Timeout";
        case LAUNCHER_UPDATE_ERROR_MAGIC_BYTE: return "Wrong Magic Byte";
        case LAUNCHER_UPDATE_ERROR_ACTIVATE: return "Could Not Activate The Firmware";
        case LAUNCHER_UPDATE_ERROR_NO_PARTITION: return "Partition Could Not be Found";
        case LAUNCHER_UPDATE_ERROR_BAD_ARGUMENT: return "Bad Argument";
        case LAUNCHER_UPDATE_ERROR_ABORT: return "Aborted";
        default: return "UNKNOWN";
    }
}

bool launcherUpdateStream(Stream &source, size_t size, LauncherUpdateTarget target, LauncherUpdateProgress cb) {
    if (!launcherUpdateBegin(target, size)) return false;

    uint8_t buffer[1024];
    size_t written = 0;
    if (cb) cb(0, size);

    while (written < size) {
        const size_t to_read = std::min(sizeof(buffer), size - written);
        const int bytes_read = source.readBytes(buffer, to_read);
        if (bytes_read <= 0) {
            setError(LAUNCHER_UPDATE_ERROR_STREAM);
            return false;
        }
        if (launcherUpdateWrite(buffer, bytes_read) != static_cast<size_t>(bytes_read)) return false;
        written += bytes_read;
        if (cb) cb(written, size);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return launcherUpdateEnd();
}

bool launcherUpdateTargetFromCommand(int command, LauncherUpdateTarget &target) {
    switch (command) {
        case LAUNCHER_UPDATE_COMMAND_FLASH:
            target = LAUNCHER_UPDATE_APP;
            return true;
        case LAUNCHER_UPDATE_COMMAND_SPIFFS:
            target = LAUNCHER_UPDATE_SPIFFS;
            return true;
        default: return false;
    }
}
