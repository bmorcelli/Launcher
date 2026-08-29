#include "idf_update.h"

#include "esp_flash.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "launcher_capabilities.h"
#include "launcher_platform.h"
#include "launcher_storage.h"
#include "partition_table_model.h"
#include <Arduino.h>
#include <algorithm>
#include <cstring>

namespace {
constexpr size_t kSectorSize = 4096;
constexpr size_t kAppHeaderHoldSize = 16;

struct LauncherRequiredBootPartition {
    const char *label;
    uint8_t type;
    uint8_t subtype;
    uint32_t offset;
    uint32_t size;
};

constexpr LauncherRequiredBootPartition kRequiredBootPartitions[] = {
    {"nvs",     0x01, 0x02, 0x9000, 0x5000},
    {"otadata", 0x01, 0x00, 0xE000, 0x2000},
};

struct LauncherUpdateContext {
    const esp_partition_t *partition = nullptr;
    LauncherUpdateTarget target = LAUNCHER_UPDATE_APP;
    uint32_t raw_address = 0;
    size_t partition_size = 0;
    size_t size = 0;
    size_t written = 0;
    int error = LAUNCHER_UPDATE_ERROR_OK;
    bool running = false;
    bool raw = false;
    bool app_header_pending = false;
    uint8_t app_header[kAppHeaderHoldSize] = {0};
    size_t app_header_len = 0;
    uint8_t raw_tail[4] = {0};
    size_t raw_tail_len = 0;
    uint32_t raw_tail_address = 0;
    size_t erased_until = 0;
};

LauncherUpdateContext ctx;

bool mutationAllowed() {
    if (launcherFirmwareMutationAllowed()) return true;
    ctx.error = LAUNCHER_UPDATE_ERROR_ABORT;
    ctx.running = false;
    launcherConsolePrintln("Firmware/partition mutation disabled on this target");
    return false;
}

size_t roundUpToSector(size_t value) { return (value + kSectorSize - 1) & ~(kSectorSize - 1); }

size_t roundDownToSector(size_t value) { return value & ~(kSectorSize - 1); }

void setError(int error) {
    ctx.error = error;
    ctx.running = false;
}

bool isBootableAppPartition(const esp_partition_t *partition) {
    uint8_t byte = 0;
    return partition && esp_partition_read(partition, 0, &byte, 1) == ESP_OK &&
           byte == ESP_IMAGE_HEADER_MAGIC;
}

bool isBootableRawApp(uint32_t address) {
    uint8_t byte = 0;
    esp_err_t err = esp_flash_read(nullptr, &byte, address, 1);
    return err == ESP_OK && byte == ESP_IMAGE_HEADER_MAGIC;
}

bool verifyAppImage(uint32_t address, size_t partitionSize) {
    esp_image_metadata_t metadata;
    const esp_partition_pos_t pos = {
        .offset = address,
        .size = static_cast<uint32_t>(partitionSize),
    };
    esp_err_t err = esp_image_verify(ESP_IMAGE_VERIFY, &pos, &metadata);
    return err == ESP_OK;
}

bool validRawRange(uint32_t address, size_t size) {
    return address != 0 && size != 0 && (address % kSectorSize) == 0 && (size % kSectorSize) == 0;
}

bool isRequiredBootPartitionLabel(const char *label) {
    for (const LauncherRequiredBootPartition &required : kRequiredBootPartitions) {
        if (strncmp(label, required.label, 16) == 0) return true;
    }
    return false;
}

bool overlapsRequiredBootPartitionArea(const LauncherPartitionEntry &entry) {
    constexpr uint32_t requiredStart = 0x9000;
    constexpr uint32_t requiredEnd = 0x10000;
    const uint32_t entryEnd = entry.offset + entry.size;
    return entry.offset < requiredEnd && requiredStart < entryEnd;
}

bool hasRequiredBootPartitions(const LauncherPartitionTable &table) {
    for (const LauncherRequiredBootPartition &required : kRequiredBootPartitions) {
        const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, required.label);
        if (!entry || entry->type != required.type || entry->subtype != required.subtype ||
            entry->offset != required.offset || entry->size != required.size) {
            return false;
        }
    }
    return true;
}

void addRequiredBootPartitions(LauncherPartitionTable &table) {
    for (const LauncherRequiredBootPartition &required : kRequiredBootPartitions) {
        LauncherPartitionEntry entry;
        entry.type = required.type;
        entry.subtype = required.subtype;
        entry.offset = required.offset;
        entry.size = required.size;
        entry.flags = 0;
        memset(entry.label, 0, sizeof(entry.label));
        strncpy(entry.label, required.label, sizeof(entry.label) - 1);
        table.entries.push_back(entry);
    }
}

bool writeRawFlashBytes(uint32_t address, const uint8_t *data, size_t len) {
    if (address < ctx.raw_address || len > ctx.partition_size ||
        address - ctx.raw_address > ctx.partition_size - len) {
        setError(LAUNCHER_UPDATE_ERROR_BAD_ARGUMENT);
        return false;
    }

    const size_t relStart = address - ctx.raw_address;
    const size_t relEnd = relStart + len;
    if (relEnd > ctx.erased_until) {
        const size_t eraseStart =
            relStart < ctx.erased_until ? ctx.erased_until : roundDownToSector(relStart);
        const size_t eraseEnd = roundUpToSector(relEnd);
        if (eraseEnd > ctx.partition_size || eraseEnd < eraseStart) {
            setError(LAUNCHER_UPDATE_ERROR_BAD_ARGUMENT);
            return false;
        }

        esp_err_t eraseErr =
            esp_flash_erase_region(nullptr, ctx.raw_address + eraseStart, eraseEnd - eraseStart);
        if (eraseErr != ESP_OK) {
            launcherDelayMs(20);
            eraseErr = esp_flash_erase_region(nullptr, ctx.raw_address + eraseStart, eraseEnd - eraseStart);
        }
        if (eraseErr != ESP_OK) {
            launcherConsolePrintf(
                "Flash erase failed at 0x%08X (%s)\n",
                (unsigned)(ctx.raw_address + eraseStart),
                esp_err_to_name(eraseErr)
            );
            setError(LAUNCHER_UPDATE_ERROR_ERASE);
            return false;
        }
        ctx.erased_until = eraseEnd;
    }

    esp_err_t err = esp_flash_write(nullptr, data, address, len);
    if (err != ESP_OK) {
        launcherDelayMs(20);
        err = esp_flash_write(nullptr, data, address, len);
    }
    if (err != ESP_OK) {
        launcherConsolePrintf("Flash write failed at 0x%08X (%s)\n", (unsigned)address, esp_err_to_name(err));
        setError(LAUNCHER_UPDATE_ERROR_WRITE);
        return false;
    }
    return true;
}

bool flushRawTail() {
    if (ctx.raw_tail_len == 0) return true;

    uint8_t padded[4];
    memset(padded, 0xFF, sizeof(padded));
    memcpy(padded, ctx.raw_tail, ctx.raw_tail_len);
    ctx.raw_tail_len = 0;
    return writeRawFlashBytes(ctx.raw_tail_address, padded, sizeof(padded));
}

bool writeRawFlash(size_t offset, const uint8_t *data, size_t len) {
    uint32_t address = ctx.raw_address + offset;
    size_t consumed = 0;

    if (ctx.raw_tail_len > 0) {
        const size_t fill = std::min(sizeof(ctx.raw_tail) - ctx.raw_tail_len, len);
        memcpy(ctx.raw_tail + ctx.raw_tail_len, data, fill);
        ctx.raw_tail_len += fill;
        consumed += fill;

        if (ctx.raw_tail_len == sizeof(ctx.raw_tail) && !flushRawTail()) return false;
    }

    const size_t remaining = len - consumed;
    const size_t aligned_len = remaining & ~(sizeof(ctx.raw_tail) - 1);
    address = ctx.raw_address + offset + consumed;
    if (aligned_len > 0) {
        if (!writeRawFlashBytes(address, data + consumed, aligned_len)) return false;
        consumed += aligned_len;
    }

    if (consumed < len) {
        ctx.raw_tail_address = ctx.raw_address + offset + consumed;
        ctx.raw_tail_len = len - consumed;
        memcpy(ctx.raw_tail, data + consumed, ctx.raw_tail_len);
    }

    return true;
}

bool writeFlash(size_t offset, const uint8_t *data, size_t len) {
    if (ctx.raw) { return writeRawFlash(offset, data, len); }

    const size_t relEnd = offset + len;
    if (relEnd > ctx.erased_until) {
        const size_t eraseStart = offset < ctx.erased_until ? ctx.erased_until : roundDownToSector(offset);
        const size_t eraseEnd = roundUpToSector(relEnd);
        if (eraseEnd > ctx.partition_size || eraseEnd < eraseStart) {
            setError(LAUNCHER_UPDATE_ERROR_BAD_ARGUMENT);
            return false;
        }

        esp_err_t eraseErr = esp_partition_erase_range(ctx.partition, eraseStart, eraseEnd - eraseStart);
        if (eraseErr != ESP_OK) {
            launcherDelayMs(20);
            eraseErr = esp_partition_erase_range(ctx.partition, eraseStart, eraseEnd - eraseStart);
        }
        if (eraseErr != ESP_OK) {
            launcherConsolePrintf(
                "Partition erase failed at offset 0x%08X (%s)\n",
                (unsigned)eraseStart,
                esp_err_to_name(eraseErr)
            );
            setError(LAUNCHER_UPDATE_ERROR_ERASE);
            return false;
        }
        ctx.erased_until = eraseEnd;
    }

    esp_err_t err = esp_partition_write(ctx.partition, offset, data, len);
    if (err != ESP_OK) {
        launcherDelayMs(20);
        err = esp_partition_write(ctx.partition, offset, data, len);
    }
    if (err != ESP_OK) {
        launcherConsolePrintf(
            "Partition write failed at offset 0x%08X (%s)\n", (unsigned)offset, esp_err_to_name(err)
        );
        setError(LAUNCHER_UPDATE_ERROR_WRITE);
        return false;
    }
    return true;
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
        if (ctx.app_header_len != kAppHeaderHoldSize) return true;
    }

    if (offset >= len) return true;

    const size_t partition_offset = ctx.written;
    const size_t write_len = len - offset;
    if (!writeFlash(partition_offset, data + offset, write_len)) return false;

    ctx.written += write_len;
    return true;
}

bool writeData(const uint8_t *data, size_t len) {
    if (ctx.target == LAUNCHER_UPDATE_APP) return writeAppDataWithDeferredHeader(data, len);

    if (!writeFlash(ctx.written, data, len)) return false;
    ctx.written += len;
    return true;
}
} // namespace

size_t launcherUpdateWrite(const uint8_t *data, size_t len) {
    if (!mutationAllowed()) return 0;
    if (!ctx.running || ctx.error != LAUNCHER_UPDATE_ERROR_OK || !data || len == 0) return 0;

    if (ctx.written >= ctx.size) return 0;
    const size_t write_len = std::min(len, ctx.size - ctx.written);

    if (!writeData(data, write_len)) return 0;
    return write_len;
}

bool launcherRawUpdateEnd() {
    if (!mutationAllowed()) return false;
    if (!ctx.running || ctx.error != LAUNCHER_UPDATE_ERROR_OK) return false;
    if (ctx.raw && !flushRawTail()) return false;
    if (ctx.written != ctx.size) {
        setError(LAUNCHER_UPDATE_ERROR_ABORT);
        return false;
    }

    if (ctx.target == LAUNCHER_UPDATE_APP) {
        if (ctx.app_header_len != kAppHeaderHoldSize) {
            setError(LAUNCHER_UPDATE_ERROR_MAGIC_BYTE);
            return false;
        }
        if (ctx.raw) {
            if (!writeRawFlashBytes(ctx.raw_address, ctx.app_header, kAppHeaderHoldSize)) return false;
        } else {
            if (!writeFlash(0, ctx.app_header, kAppHeaderHoldSize)) return false;
        }
        if (ctx.raw && !flushRawTail()) return false;
        if (ctx.raw ? !isBootableRawApp(ctx.raw_address) : !isBootableAppPartition(ctx.partition)) {
            setError(LAUNCHER_UPDATE_ERROR_READ);
            return false;
        }
        uint32_t address = ctx.raw ? ctx.raw_address : ctx.partition->address;
        if (!verifyAppImage(address, ctx.partition_size)) {
            setError(LAUNCHER_UPDATE_ERROR_READ);
            return false;
        }

        if (!ctx.raw) {
            esp_err_t err = esp_ota_set_boot_partition(ctx.partition);
            if (err != ESP_OK) {
                setError(LAUNCHER_UPDATE_ERROR_ACTIVATE);
                return false;
            }
        }
    }

    ctx.running = false;
    return true;
}

void launcherUpdateAbort() { setError(LAUNCHER_UPDATE_ERROR_ABORT); }

bool launcherUpdateIsFinished() {
    return ctx.size > 0 && ctx.written == ctx.size && ctx.error == LAUNCHER_UPDATE_ERROR_OK;
}

int launcherUpdateLastError() { return ctx.error; }

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

bool launcherRawUpdateBegin(uint32_t address, size_t partitionSize, size_t imageSize, bool appImage) {
    ctx = LauncherUpdateContext();
    if (!mutationAllowed()) return false;
    ctx.raw = true;
    ctx.raw_address = address;
    ctx.partition_size = partitionSize;
    ctx.size = imageSize;
    ctx.target = appImage ? LAUNCHER_UPDATE_APP : LAUNCHER_UPDATE_DATA;

    if (address == 0 || partitionSize == 0 || imageSize == 0) {
        setError(LAUNCHER_UPDATE_ERROR_BAD_ARGUMENT);
        return false;
    }
    if (imageSize > partitionSize) {
        setError(LAUNCHER_UPDATE_ERROR_SIZE);
        return false;
    }
    if ((address % kSectorSize) != 0 || roundUpToSector(imageSize) > partitionSize) {
        setError(LAUNCHER_UPDATE_ERROR_BAD_ARGUMENT);
        return false;
    }

    ctx.running = true;
    ctx.error = LAUNCHER_UPDATE_ERROR_OK;
    ctx.app_header_pending = appImage;

    return true;
}

size_t launcherRawUpdateWrite(const uint8_t *data, size_t len) { return launcherUpdateWrite(data, len); }

bool launcherRawErase(uint32_t address, size_t size) {
    if (!mutationAllowed()) return false;
    ctx.error = LAUNCHER_UPDATE_ERROR_OK;
    if (!validRawRange(address, size)) {
        ctx.error = LAUNCHER_UPDATE_ERROR_BAD_ARGUMENT;
        return false;
    }

    esp_err_t err = esp_flash_erase_region(nullptr, address, size);
    if (err != ESP_OK) {
        ctx.error = LAUNCHER_UPDATE_ERROR_ERASE;
        return false;
    }
    return true;
}

bool launcherRawPrepareDataPartition(uint32_t address, size_t size) {
    return launcherRawErase(address, size);
}

bool launcherUpdateErasePartition(const esp_partition_t *partition) {
    if (!mutationAllowed()) return false;
    ctx.error = LAUNCHER_UPDATE_ERROR_OK;
    if (!partition) {
        ctx.error = LAUNCHER_UPDATE_ERROR_NO_PARTITION;
        return false;
    }

    esp_err_t err = esp_partition_erase_range(partition, 0, partition->size);
    if (err != ESP_OK) {
        ctx.error = LAUNCHER_UPDATE_ERROR_ERASE;
        return false;
    }
    return true;
}

bool launcherUpdateCopyPartition(
    const esp_partition_t *source, const esp_partition_t *destination, LauncherUpdateProgress cb
) {
    if (!mutationAllowed()) return false;
    ctx.error = LAUNCHER_UPDATE_ERROR_OK;
    if (!source || !destination) {
        ctx.error = LAUNCHER_UPDATE_ERROR_NO_PARTITION;
        return false;
    }

    constexpr size_t bufferSize = 1024;
    uint8_t buffer[bufferSize];
    const size_t copySize = std::min(source->size, destination->size);
    size_t copied = 0;
    if (cb) cb(0, copySize);

    while (copied < copySize) {
        const size_t chunk = std::min(bufferSize, copySize - copied);
        esp_err_t err = esp_partition_read(source, copied, buffer, chunk);
        if (err != ESP_OK) {
            ctx.error = LAUNCHER_UPDATE_ERROR_READ;
            return false;
        }

        err = esp_partition_write(destination, copied, buffer, chunk);
        if (err != ESP_OK) {
            ctx.error = LAUNCHER_UPDATE_ERROR_WRITE;
            return false;
        }

        copied += chunk;
        if (cb) cb(copied, copySize);
        launcherDelayMs(1);
    }
    return true;
}

bool launcherUpdateRepairPartitionTable(uint32_t removeOtaAddress, bool *removedOta) {
    if (!mutationAllowed()) return false;
    ctx.error = LAUNCHER_UPDATE_ERROR_OK;
    if (removedOta) *removedOta = false;

    String error;
    LauncherPartitionTable current;
    if (!launcherPartitionReadCurrentUnchecked(current, &error)) {
        launcherConsolePrintf("Partition table read failed: %s\n", error.c_str());
        ctx.error = LAUNCHER_UPDATE_ERROR_READ;
        return false;
    }

    const bool tableValid = launcherPartitionValidate(current, &error);
    const bool repairBootPartitions = !tableValid || !hasRequiredBootPartitions(current);
    bool removed = false;

    LauncherPartitionTable target = current;
    target.entries.erase(
        std::remove_if(
            target.entries.begin(),
            target.entries.end(),
            [&](const LauncherPartitionEntry &entry) {
                if (removeOtaAddress != 0 && entry.isOtaApp() && entry.offset == removeOtaAddress) {
                    removed = true;
                    return true;
                }
                if (!repairBootPartitions) return false;
                return isRequiredBootPartitionLabel(entry.label) || overlapsRequiredBootPartitionArea(entry);
            }
        ),
        target.entries.end()
    );

    if (!repairBootPartitions && !removed) return true;
    if (repairBootPartitions) addRequiredBootPartitions(target);

    std::sort(
        target.entries.begin(),
        target.entries.end(),
        [](const LauncherPartitionEntry &a, const LauncherPartitionEntry &b) { return a.offset < b.offset; }
    );

    if (!launcherPartitionValidate(target, &error)) {
        launcherConsolePrintf("Partition table validation failed: %s\n", error.c_str());
        ctx.error = LAUNCHER_UPDATE_ERROR_BAD_ARGUMENT;
        return false;
    }

    if (!launcherPartitionWriteGeneratedTable(target, &error)) {
        launcherConsolePrintf("Partition table write failed: %s\n", error.c_str());
        ctx.error = LAUNCHER_UPDATE_ERROR_WRITE;
        return false;
    }

    if (removedOta) *removedOta = removed;
    launcherConsolePrintf(
        "Partition table updated: boot_repair=%d removed_ota=%d\n", repairBootPartitions, removed
    );
    return true;
}

bool launcherClearCoredump() {
    if (!mutationAllowed()) return false;
    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "coredump");
    launcherConsolePrintf("Coredump partition address: 0x%08X\n", partition ? partition->address : 0);
    if (!partition) {
        launcherConsolePrintln("Failed to find coredump partition");
        log_e("Failed to find coredump partition");
        return false;
    }

    log_i("Erasing coredump partition at address 0x%08X, size %d bytes", partition->address, partition->size);
    esp_err_t err = esp_partition_erase_range(partition, 0, partition->size);
    if (err != ESP_OK) {
        launcherConsolePrintln("Failed to erase coredump partition");
        log_e("Failed to erase coredump partition: %s", esp_err_to_name(err));
        return false;
    }

    launcherConsolePrintln("Coredump partition cleared successfully");
    log_i("Coredump partition cleared successfully");
    return true;
}

bool launcherRawUpdateStream(
    Stream &source, uint32_t address, size_t partitionSize, size_t imageSize, bool appImage,
    LauncherUpdateProgress cb
) {
    if (!mutationAllowed()) return false;
    if (!launcherRawUpdateBegin(address, partitionSize, imageSize, appImage)) return false;

    uint8_t buffer[1024];
    size_t written = 0;
    if (cb) cb(0, imageSize);

    while (written < imageSize) {
        const size_t to_read = std::min(sizeof(buffer), imageSize - written);

        int bytes_read = source.readBytes(buffer, to_read);
        if (bytes_read <= 0) {
            launcherDelayMs(20);
            bytes_read = source.readBytes(buffer, to_read);
        }
        if (bytes_read <= 0) {
            launcherConsolePrintln("Update stream: read failed");
            setError(LAUNCHER_UPDATE_ERROR_STREAM);
            return false;
        }
        if (launcherRawUpdateWrite(buffer, bytes_read) != static_cast<size_t>(bytes_read)) {
            launcherConsolePrintf(
                "Update stream: flash write failed at partition offset 0x%08X (%s)\n",
                (unsigned)written,
                launcherUpdateLastErrorName()
            );
            return false;
        }
        written += bytes_read;
        if (cb) cb(written, imageSize);
        launcherDelayMs(1);
    }

    return launcherRawUpdateEnd();
}

bool launcherInstallAppToPartition(
    const String &sourcePath, const esp_partition_t *partition, size_t imageSize, LauncherUpdateProgress cb
) {
    if (!launcherFirmwareInstallAllowed()) {
        launcherConsolePrintln("PM_INSTALL_FAIL source-validation install-disabled");
        return false;
    }
    if (!partition || partition->type != ESP_PARTITION_TYPE_APP || imageSize == 0 ||
        imageSize > partition->size) {
        launcherConsolePrintln("PM_INSTALL_FAIL destination-partition");
        return false;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(partition, imageSize, &handle);
    if (err != ESP_OK) {
        launcherConsolePrintf("PM_INSTALL_FAIL ota-begin %s\n", esp_err_to_name(err));
        return false;
    }

    uint8_t buffer[4096];
    size_t written = 0;
    if (cb) cb(0, imageSize);
    while (written < imageSize) {
        const size_t requested = std::min(sizeof(buffer), imageSize - written);
        const LauncherStorageFileResult readResult =
            launcherStorageReadAt(sourcePath, written, buffer, requested);
        if (readResult != LauncherStorageFileResult::Ready) {
            launcherConsolePrintf(
                "PM_INSTALL_FAIL source-read offset=%u expected=%u actual=%u\n",
                static_cast<unsigned>(written),
                static_cast<unsigned>(requested),
                0U
            );
            esp_ota_abort(handle);
            return false;
        }
        const size_t received = requested;
        err = esp_ota_write(handle, buffer, received);
        if (err != ESP_OK) {
            launcherConsolePrintf(
                "PM_INSTALL_FAIL ota-write %s offset=%u\n",
                esp_err_to_name(err),
                static_cast<unsigned>(written)
            );
            esp_ota_abort(handle);
            return false;
        }
        written += received;
        if (cb) cb(written, imageSize);
    }
    launcherConsolePrintf("PM_INSTALL_WRITE_DONE bytes=%u\n", static_cast<unsigned>(written));

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        launcherConsolePrintf("PM_INSTALL_FAIL ota-end %s\n", esp_err_to_name(err));
        return false;
    }

    esp_image_metadata_t metadata;
    const esp_partition_pos_t position = {
        .offset = partition->address,
        .size = partition->size,
    };
    err = esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata);
    if (err != ESP_OK) {
        launcherConsolePrintf("PM_INSTALL_FAIL image-verify %s\n", esp_err_to_name(err));
        return false;
    }
    launcherConsolePrintln("PM_INSTALL_VERIFY_PASS");

    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        launcherConsolePrintf("PM_INSTALL_FAIL set-boot %s\n", esp_err_to_name(err));
        return false;
    }
    launcherConsolePrintln("PM_INSTALL_BOOT_SELECTED");
    return true;
}
