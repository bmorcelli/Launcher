#include "partition_table_model.h"

#include "idf/launcher_platform.h"
#include "pre_compiler.h"

#include <algorithm>
#include <bootloader_common.h>
#include <cstring>
#include <esp_flash.h>
#include <esp_flash_partitions.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/md5.h>

namespace {
constexpr uint16_t kPartitionMagic = ESP_PARTITION_MAGIC;
constexpr uint16_t kPartitionMagicMd5 = ESP_PARTITION_MAGIC_MD5;
constexpr uint8_t kTypeApp = PART_TYPE_APP;
constexpr uint8_t kTypeData = PART_TYPE_DATA;
constexpr uint8_t kSubtypeFactory = PART_SUBTYPE_FACTORY;
constexpr uint8_t kSubtypeTest = PART_SUBTYPE_TEST;
constexpr uint8_t kSubtypeOtaFlag = PART_SUBTYPE_OTA_FLAG;
constexpr uint8_t kSubtypeOtaMask = PART_SUBTYPE_OTA_MASK;
constexpr uint8_t kSubtypeDataOta = PART_SUBTYPE_DATA_OTA;
constexpr uint8_t kSubtypeDataNvs = PART_SUBTYPE_DATA_NVS_KEYS;
constexpr uint8_t kSubtypeDataRf = PART_SUBTYPE_DATA_RF;
constexpr uint8_t kSubtypeDataWifi = PART_SUBTYPE_DATA_WIFI;
constexpr uint8_t kSubtypeDataEfuse = PART_SUBTYPE_DATA_EFUSE_EM;
uint8_t gPartitionTableRaw[LAUNCHER_PARTITION_TABLE_SIZE];
uint8_t gPartitionTableVerify[LAUNCHER_PARTITION_TABLE_SIZE];
uint8_t gPartitionMoveBuffer[LAUNCHER_FLASH_SECTOR_SIZE];

uint16_t readU16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readU32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void writeU16(uint8_t *p, uint16_t value) {
    p[0] = value & 0xFF;
    p[1] = (value >> 8) & 0xFF;
}

void writeU32(uint8_t *p, uint32_t value) {
    p[0] = value & 0xFF;
    p[1] = (value >> 8) & 0xFF;
    p[2] = (value >> 16) & 0xFF;
    p[3] = (value >> 24) & 0xFF;
}

bool aligned(uint32_t value, uint32_t alignment) { return alignment > 0 && (value % alignment) == 0; }

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

bool isProtectedDataSubtype(uint8_t subtype) {
    return subtype == kSubtypeDataOta || subtype == kSubtypeDataNvs || subtype == kSubtypeDataRf ||
           subtype == kSubtypeDataWifi || subtype == kSubtypeDataEfuse;
}

bool samePartitionIdentity(const LauncherPartitionEntry &a, const LauncherPartitionEntry &b) {
    return a.type == b.type && strncmp(a.label, b.label, 16) == 0;
}

const LauncherPartitionEntry *
findSourcePartition(const LauncherPartitionTable &table, const LauncherPartitionEntry &target) {
    const LauncherPartitionEntry *fallback = nullptr;
    for (const LauncherPartitionEntry &entry : table.entries) {
        if (!samePartitionIdentity(entry, target)) continue;
        if (entry.subtype == target.subtype) return &entry;
        if (!fallback) fallback = &entry;
    }
    return fallback;
}

void setError(String *error, const char *message) {
    if (error) *error = message;
}

bool getFlashSize(uint32_t &flashSize, String *error) {
    uint32_t detected = 0;
    esp_err_t err = esp_flash_get_size(nullptr, &detected);
    if (err != ESP_OK || detected == 0) {
        setError(error, "Could not detect flash size");
        return false;
    }
    flashSize = detected;
    return true;
}

bool md5Matches(const uint8_t *data, size_t len, const uint8_t *expected) {
    uint8_t digest[16] = {0};
    return mbedtls_md5(data, len, digest) == 0 && memcmp(digest, expected, sizeof(digest)) == 0;
}
} // namespace

bool LauncherPartitionEntry::isApp() const { return type == kTypeApp; }

bool LauncherPartitionEntry::isData() const { return type == kTypeData; }

bool LauncherPartitionEntry::isOtaApp() const {
    return isApp() && subtype >= kSubtypeOtaFlag && subtype <= (kSubtypeOtaFlag | kSubtypeOtaMask);
}

bool LauncherPartitionEntry::isFactoryOrTestApp() const {
    return isApp() && (subtype == kSubtypeFactory || subtype == kSubtypeTest);
}

bool launcherPartitionReadCurrent(LauncherPartitionTable &table, String *error) {
    esp_err_t err = esp_flash_read(
        nullptr, gPartitionTableRaw, LAUNCHER_PARTITION_TABLE_OFFSET, sizeof(gPartitionTableRaw)
    );
    if (err != ESP_OK) {
        setError(error, "Could not read partition table from flash");
        return false;
    }
    return launcherPartitionParse(gPartitionTableRaw, sizeof(gPartitionTableRaw), table, error);
}

bool launcherPartitionParse(const uint8_t *data, size_t size, LauncherPartitionTable &table, String *error) {
    table = LauncherPartitionTable();
    if (!data || size < LAUNCHER_PARTITION_ENTRY_SIZE) {
        setError(error, "Partition table buffer is too small");
        return false;
    }

    if (!getFlashSize(table.flashSize, error)) return false;

    for (size_t pos = 0; pos + LAUNCHER_PARTITION_ENTRY_SIZE <= size; pos += LAUNCHER_PARTITION_ENTRY_SIZE) {
        const uint8_t *entry = data + pos;
        uint16_t magic = readU16(entry);

        if (magic == kPartitionMagicMd5) {
            table.hasMd5 = true;
            if (pos + ESP_PARTITION_MD5_OFFSET + 16 > size) {
                setError(error, "Partition table MD5 entry is truncated");
                return false;
            }
            if (!md5Matches(data, pos, entry + ESP_PARTITION_MD5_OFFSET)) {
                setError(error, "Partition table MD5 mismatch");
                return false;
            }
            return launcherPartitionValidate(table, error);
        }

        if (magic == 0xFFFF || magic == 0x0000) { return launcherPartitionValidate(table, error); }

        if (magic != kPartitionMagic) {
            setError(error, "Invalid partition entry magic");
            return false;
        }

        LauncherPartitionEntry parsed;
        parsed.type = entry[2];
        parsed.subtype = entry[3];
        parsed.offset = readU32(entry + 4);
        parsed.size = readU32(entry + 8);
        memcpy(parsed.label, entry + 12, 16);
        parsed.label[16] = '\0';
        parsed.flags = readU32(entry + 28);
        table.entries.push_back(parsed);
    }

    setError(error, "Partition table terminator not found");
    return false;
}

bool launcherPartitionBuild(
    const LauncherPartitionTable &table, uint8_t *out, size_t outSize, String *error
) {
    if (!out || outSize < LAUNCHER_PARTITION_TABLE_SIZE) {
        setError(error, "Output partition table buffer is too small");
        return false;
    }
    if (!launcherPartitionValidate(table, error)) return false;

    const size_t entryBytes = table.entries.size() * LAUNCHER_PARTITION_ENTRY_SIZE;
    const size_t md5EntryOffset = entryBytes;
    if (md5EntryOffset + LAUNCHER_PARTITION_ENTRY_SIZE > outSize) {
        setError(error, "Generated partition table does not fit");
        return false;
    }

    memset(out, 0xFF, outSize);

    for (size_t i = 0; i < table.entries.size(); ++i) {
        const LauncherPartitionEntry &src = table.entries[i];
        uint8_t *dst = out + i * LAUNCHER_PARTITION_ENTRY_SIZE;
        writeU16(dst, kPartitionMagic);
        dst[2] = src.type;
        dst[3] = src.subtype;
        writeU32(dst + 4, src.offset);
        writeU32(dst + 8, src.size);
        memset(dst + 12, 0, 16);
        strncpy(reinterpret_cast<char *>(dst + 12), src.label, 15);
        writeU32(dst + 28, src.flags);
    }

    uint8_t *md5 = out + md5EntryOffset;
    writeU16(md5, kPartitionMagicMd5);
    if (mbedtls_md5(out, md5EntryOffset, md5 + ESP_PARTITION_MD5_OFFSET) != 0) {
        setError(error, "Could not calculate partition table MD5");
        return false;
    }

    return true;
}

bool launcherPartitionValidate(const LauncherPartitionTable &table, String *error) {
    uint32_t flashSize = table.flashSize;
    if (flashSize == 0 && !getFlashSize(flashSize, error)) return false;

    if (table.entries.empty()) {
        setError(error, "Partition table has no entries");
        return false;
    }

    if ((table.entries.size() + 1) * LAUNCHER_PARTITION_ENTRY_SIZE > ESP_PARTITION_TABLE_MAX_LEN) {
        setError(error, "Partition table has too many entries");
        return false;
    }

    bool hasProtectedLauncher = false;
    bool hasOtaData = false;

    for (size_t i = 0; i < table.entries.size(); ++i) {
        const LauncherPartitionEntry &entry = table.entries[i];
        if (entry.size == 0) {
            setError(error, "Partition has zero size");
            return false;
        }
        if (!aligned(entry.offset, LAUNCHER_FLASH_SECTOR_SIZE) ||
            !aligned(entry.size, LAUNCHER_FLASH_SECTOR_SIZE)) {
            setError(error, "Partition offset or size is not sector aligned");
            return false;
        }
        if (entry.offset < LAUNCHER_PARTITION_TABLE_OFFSET + LAUNCHER_PARTITION_TABLE_SIZE) {
            setError(error, "Partition overlaps bootloader or partition table area");
            return false;
        }
        if (entry.offset > flashSize || entry.size > flashSize || entry.offset + entry.size > flashSize) {
            setError(error, "Partition exceeds flash size");
            return false;
        }
        if (entry.label[0] == '\0') {
            setError(error, "Partition label is empty");
            return false;
        }
        if (entry.isFactoryOrTestApp()) hasProtectedLauncher = true;
        if (entry.isData() && entry.subtype == kSubtypeDataOta) hasOtaData = true;

        for (size_t j = i + 1; j < table.entries.size(); ++j) {
            const LauncherPartitionEntry &other = table.entries[j];
            uint32_t aEnd = entry.offset + entry.size;
            uint32_t bEnd = other.offset + other.size;
            if (entry.offset < bEnd && other.offset < aEnd) {
                setError(error, "Partition ranges overlap");
                return false;
            }
            if (strncmp(entry.label, other.label, 16) == 0 && !entry.isOtaApp() && !other.isOtaApp()) {
                setError(error, "Duplicate partition label");
                return false;
            }
        }

        if (entry.isApp() && !aligned(entry.offset, 0x10000)) {
            setError(error, "App partition offset is not 64 KB aligned");
            return false;
        }
        if (entry.isData() && isProtectedDataSubtype(entry.subtype) &&
            entry.size < LAUNCHER_FLASH_SECTOR_SIZE) {
            setError(error, "Protected data partition is too small");
            return false;
        }
    }

    if (!hasProtectedLauncher) {
        setError(error, "Partition table must keep a FACTORY or TEST Launcher partition");
        return false;
    }
    if (!hasOtaData) {
        setError(error, "Partition table must keep otadata");
        return false;
    }

    return true;
}

uint32_t launcherPartitionAlignment(uint8_t type, uint8_t subtype) {
    if (type == kTypeApp) return LAUNCHER_APP_PARTITION_ALIGNMENT;
    if (type == kTypeData &&
        (subtype == ESP_PARTITION_SUBTYPE_DATA_FAT || subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS ||
         subtype == ESP_PARTITION_SUBTYPE_DATA_LITTLEFS)) {
        return LAUNCHER_APP_PARTITION_ALIGNMENT;
    }
    return LAUNCHER_FLASH_SECTOR_SIZE;
}

bool launcherPartitionCompact(LauncherPartitionTable &table, String *error) {
    uint32_t flashSize = table.flashSize;
    if (flashSize == 0 && !getFlashSize(flashSize, error)) return false;
    table.flashSize = flashSize;

    std::sort(
        table.entries.begin(),
        table.entries.end(),
        [](const LauncherPartitionEntry &a, const LauncherPartitionEntry &b) { return a.offset < b.offset; }
    );

    uint32_t cursor = LAUNCHER_PARTITION_TABLE_OFFSET + LAUNCHER_PARTITION_TABLE_SIZE;
    for (LauncherPartitionEntry &entry : table.entries) {
        cursor = alignUp(cursor, launcherPartitionAlignment(entry.type, entry.subtype));
        if (cursor > flashSize || entry.size > flashSize || cursor + entry.size > flashSize) {
            setError(error, "Compacted partition table exceeds flash size");
            return false;
        }
        if (entry.offset != cursor) { entry.offset = cursor; }
        cursor += entry.size;
    }

    return launcherPartitionValidate(table, error);
}

bool launcherPartitionMigrateMovedData(
    const LauncherPartitionTable &currentTable, const LauncherPartitionTable &targetTable, String *error
) {
    if (!launcherPartitionValidate(currentTable, error) || !launcherPartitionValidate(targetTable, error)) {
        return false;
    }

    for (const LauncherPartitionEntry &target : targetTable.entries) {
        const LauncherPartitionEntry *source = findSourcePartition(currentTable, target);
        if (!source) continue;
        if (source->offset == target.offset) continue;

        const uint32_t copySize = std::min(source->size, target.size);
        if (copySize == 0) continue;
        if ((source->offset % LAUNCHER_FLASH_SECTOR_SIZE) != 0 ||
            (target.offset % LAUNCHER_FLASH_SECTOR_SIZE) != 0 ||
            (copySize % LAUNCHER_FLASH_SECTOR_SIZE) != 0) {
            setError(error, "Partition move is not sector aligned");
            return false;
        }

        launcherConsolePrintf(
            "Moving partition label=%s type=0x%02X subtype=0x%02X from=0x%08X to=0x%08X size=0x%08X\n",
            target.label,
            target.type,
            target.subtype,
            source->offset,
            target.offset,
            copySize
        );

        const bool copyForward = target.offset < source->offset;
        if (copyForward) {
            for (uint32_t offset = 0; offset < copySize; offset += LAUNCHER_FLASH_SECTOR_SIZE) {
                esp_err_t err = esp_flash_read(
                    nullptr, gPartitionMoveBuffer, source->offset + offset, sizeof(gPartitionMoveBuffer)
                );
                if (err != ESP_OK) {
                    setError(error, "Could not read partition while moving");
                    return false;
                }
                err = esp_flash_erase_region(nullptr, target.offset + offset, LAUNCHER_FLASH_SECTOR_SIZE);
                if (err != ESP_OK) {
                    setError(error, "Could not erase destination while moving");
                    return false;
                }
                err = esp_flash_write(
                    nullptr, gPartitionMoveBuffer, target.offset + offset, sizeof(gPartitionMoveBuffer)
                );
                if (err != ESP_OK) {
                    setError(error, "Could not write partition while moving");
                    return false;
                }
                yield();
            }
        } else {
            for (uint32_t remaining = copySize; remaining > 0; remaining -= LAUNCHER_FLASH_SECTOR_SIZE) {
                const uint32_t offset = remaining - LAUNCHER_FLASH_SECTOR_SIZE;
                esp_err_t err = esp_flash_read(
                    nullptr, gPartitionMoveBuffer, source->offset + offset, sizeof(gPartitionMoveBuffer)
                );
                if (err != ESP_OK) {
                    setError(error, "Could not read partition while moving");
                    return false;
                }
                err = esp_flash_erase_region(nullptr, target.offset + offset, LAUNCHER_FLASH_SECTOR_SIZE);
                if (err != ESP_OK) {
                    setError(error, "Could not erase destination while moving");
                    return false;
                }
                err = esp_flash_write(
                    nullptr, gPartitionMoveBuffer, target.offset + offset, sizeof(gPartitionMoveBuffer)
                );
                if (err != ESP_OK) {
                    setError(error, "Could not write partition while moving");
                    return false;
                }
                yield();
            }
        }
    }

    return true;
}

bool launcherPartitionWriteGeneratedTable(const LauncherPartitionTable &table, String *error) {
    if (!launcherPartitionBuild(table, gPartitionTableRaw, sizeof(gPartitionTableRaw), error)) return false;

    if (gPartitionTableRaw[0] != 0xAA || gPartitionTableRaw[1] != 0x50) {
        setError(error, "Generated partition table has invalid magic");
        return false;
    }

    constexpr size_t kWriteChunk = 256;
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        esp_err_t err =
            esp_flash_erase_region(nullptr, LAUNCHER_PARTITION_TABLE_OFFSET, LAUNCHER_PARTITION_TABLE_SIZE);
        if (err != ESP_OK) {
            setError(error, "Could not erase partition table sector");
            continue;
        }

        bool writeOk = true;
        for (size_t offset = 0; offset < sizeof(gPartitionTableRaw); offset += kWriteChunk) {
            size_t len = std::min(kWriteChunk, sizeof(gPartitionTableRaw) - offset);
            err = esp_flash_write(
                nullptr, gPartitionTableRaw + offset, LAUNCHER_PARTITION_TABLE_OFFSET + offset, len
            );
            if (err != ESP_OK) {
                writeOk = false;
                break;
            }
        }
        if (!writeOk) {
            setError(error, "Could not write generated partition table");
            continue;
        }

        err = esp_flash_read(
            nullptr, gPartitionTableVerify, LAUNCHER_PARTITION_TABLE_OFFSET, sizeof(gPartitionTableVerify)
        );
        if (err == ESP_OK &&
            memcmp(gPartitionTableRaw, gPartitionTableVerify, sizeof(gPartitionTableRaw)) == 0) {
            return true;
        }
        setError(error, "Partition table verify failed");
    }

    return false;
}

LauncherPartitionEntry *launcherPartitionFindByLabel(LauncherPartitionTable &table, const char *label) {
    if (!label) return nullptr;
    for (LauncherPartitionEntry &entry : table.entries) {
        if (strncmp(entry.label, label, 16) == 0) return &entry;
    }
    return nullptr;
}

const LauncherPartitionEntry *
launcherPartitionFindByLabel(const LauncherPartitionTable &table, const char *label) {
    if (!label) return nullptr;
    for (const LauncherPartitionEntry &entry : table.entries) {
        if (strncmp(entry.label, label, 16) == 0) return &entry;
    }
    return nullptr;
}

LauncherPartitionEntry *launcherPartitionFindAppBySubtype(LauncherPartitionTable &table, uint8_t subtype) {
    for (LauncherPartitionEntry &entry : table.entries) {
        if (entry.isApp() && entry.subtype == subtype) return &entry;
    }
    return nullptr;
}

const LauncherPartitionEntry *
launcherPartitionFindAppBySubtype(const LauncherPartitionTable &table, uint8_t subtype) {
    for (const LauncherPartitionEntry &entry : table.entries) {
        if (entry.isApp() && entry.subtype == subtype) return &entry;
    }
    return nullptr;
}

int launcherPartitionOtaIndex(uint8_t subtype) {
    if (subtype < kSubtypeOtaFlag || subtype > (kSubtypeOtaFlag | kSubtypeOtaMask)) return -1;
    return subtype & kSubtypeOtaMask;
}

int launcherPartitionNextOtaSubtype(const LauncherPartitionTable &table) {
    for (uint8_t i = 0; i < 16; ++i) {
        uint8_t subtype = kSubtypeOtaFlag + i;
        if (!launcherPartitionFindAppBySubtype(table, subtype)) return subtype;
    }
    return -1;
}

const LauncherPartitionEntry *launcherPartitionFindOtaData(const LauncherPartitionTable &table) {
    for (const LauncherPartitionEntry &entry : table.entries) {
        if (entry.isData() && entry.subtype == kSubtypeDataOta) return &entry;
    }
    return nullptr;
}

uint8_t launcherPartitionCountOtaApps(const LauncherPartitionTable &table) {
    uint8_t count = 0;
    while (count < 16) {
        if (!launcherPartitionFindAppBySubtype(table, kSubtypeOtaFlag + count)) break;
        count++;
    }
    return count;
}

std::vector<LauncherPartitionRange> launcherPartitionFreeRanges(const LauncherPartitionTable &table) {
    std::vector<LauncherPartitionEntry> entries = table.entries;
    std::sort(
        entries.begin(), entries.end(), [](const LauncherPartitionEntry &a, const LauncherPartitionEntry &b) {
            return a.offset < b.offset;
        }
    );

    std::vector<LauncherPartitionRange> ranges;
    uint32_t cursor = LAUNCHER_PARTITION_TABLE_OFFSET + LAUNCHER_PARTITION_TABLE_SIZE;
    for (const LauncherPartitionEntry &entry : entries) {
        if (entry.offset > cursor) ranges.push_back({cursor, entry.offset - cursor});
        uint32_t end = entry.offset + entry.size;
        if (end > cursor) cursor = end;
    }

    uint32_t flashSize = table.flashSize;
    if (flashSize == 0) getFlashSize(flashSize, nullptr);
    if (flashSize > cursor) ranges.push_back({cursor, flashSize - cursor});
    return ranges;
}

bool launcherPartitionFindFreeRange(
    const LauncherPartitionTable &table, uint32_t requiredSize, uint32_t alignment,
    LauncherPartitionRange &range, String *error
) {
    if (requiredSize == 0) {
        setError(error, "Required partition size is zero");
        return false;
    }
    if (alignment == 0) alignment = LAUNCHER_FLASH_SECTOR_SIZE;

    for (const LauncherPartitionRange &candidate : launcherPartitionFreeRanges(table)) {
        uint32_t alignedOffset = alignUp(candidate.offset, alignment);
        if (alignedOffset < candidate.offset) continue;
        uint32_t padding = alignedOffset - candidate.offset;
        if (candidate.size >= padding && candidate.size - padding >= requiredSize) {
            range = {alignedOffset, requiredSize};
            return true;
        }
    }

    setError(error, "No free partition range large enough");
    return false;
}

bool launcherPartitionAdd(LauncherPartitionTable &table, const LauncherPartitionEntry &entry, String *error) {
    table.entries.push_back(entry);
    if (!launcherPartitionValidate(table, error)) {
        table.entries.pop_back();
        return false;
    }
    std::sort(
        table.entries.begin(),
        table.entries.end(),
        [](const LauncherPartitionEntry &a, const LauncherPartitionEntry &b) { return a.offset < b.offset; }
    );
    return true;
}

bool launcherPartitionCreateOtaApp(
    LauncherPartitionTable &table, uint32_t imageSize, const char *label, LauncherPartitionEntry *created,
    String *error
) {
    int subtype = launcherPartitionNextOtaSubtype(table);
    if (subtype < 0) {
        setError(error, "No OTA app subtype available");
        return false;
    }

    uint32_t partitionSize = alignUp(imageSize, LAUNCHER_APP_PARTITION_ALIGNMENT);
    LauncherPartitionRange range;
    if (!launcherPartitionFindFreeRange(table, partitionSize, LAUNCHER_APP_PARTITION_ALIGNMENT, range, error))
        return false;

    LauncherPartitionEntry entry;
    entry.type = kTypeApp;
    entry.subtype = static_cast<uint8_t>(subtype);
    entry.offset = range.offset;
    entry.size = partitionSize;
    entry.flags = 0;
    memset(entry.label, 0, sizeof(entry.label));
    if (label && label[0]) {
        strncpy(entry.label, label, 15);
    } else {
        snprintf(entry.label, sizeof(entry.label), "app%d", launcherPartitionOtaIndex(entry.subtype));
    }

    if (!launcherPartitionAdd(table, entry, error)) return false;
    if (created) *created = entry;
    return true;
}

bool launcherPartitionCreateData(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, uint32_t size,
    LauncherPartitionEntry *created, String *error
) {
    if (!label || !label[0]) {
        setError(error, "Data partition label is required");
        return false;
    }

    uint32_t partitionSize = alignUp(size, LAUNCHER_FLASH_SECTOR_SIZE);
    LauncherPartitionRange range;
    if (!launcherPartitionFindFreeRange(table, partitionSize, LAUNCHER_FLASH_SECTOR_SIZE, range, error)) {
        return false;
    }

    LauncherPartitionEntry entry;
    entry.type = kTypeData;
    entry.subtype = subtype;
    entry.offset = range.offset;
    entry.size = partitionSize;
    entry.flags = 0;
    memset(entry.label, 0, sizeof(entry.label));
    strncpy(entry.label, label, 15);

    if (!launcherPartitionAdd(table, entry, error)) return false;
    if (created) *created = entry;
    return true;
}

String launcherPartitionSanitizedAppLabelBase(const String &name) {
    String base;
    for (size_t i = 0; i < name.length() && base.length() < 6; ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) base += c;
    }
    if (base.isEmpty()) base = "app";
    while (base.length() < 6) base += "0";
    return base;
}

bool launcherPartitionLabelExists(const LauncherPartitionTable &table, const String &label) {
    for (const LauncherPartitionEntry &entry : table.entries) {
        if (label == entry.label) return true;
    }
    return false;
}

String launcherPartitionNextAppLabel(const LauncherPartitionTable &table, const String &installedName) {
    String base = launcherPartitionSanitizedAppLabelBase(installedName);
    if (!launcherPartitionLabelExists(table, base)) return base;

    String prefix = base.substring(0, 5);
    for (int i = 1; i <= 9; ++i) {
        String candidate = prefix + String(i);
        if (!launcherPartitionLabelExists(table, candidate)) return candidate;
    }
    String candidate = prefix + "0";
    if (!launcherPartitionLabelExists(table, candidate)) return candidate;

    for (int i = 1; i < 100; ++i) {
        candidate = "app" + String(i);
        if (!launcherPartitionLabelExists(table, candidate)) return candidate;
    }
    return "app";
}

bool launcherPartitionRenameEntryByOffset(
    LauncherPartitionTable &table, uint32_t offset, const String &label
) {
    for (LauncherPartitionEntry &entry : table.entries) {
        if (entry.offset != offset) continue;
        memset(entry.label, 0, sizeof(entry.label));
        strncpy(entry.label, label.c_str(), sizeof(entry.label) - 1);
        return true;
    }
    return false;
}

bool launcherPartitionFindOrCreateData(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, uint32_t requestedSize,
    LauncherPartitionEntry &entry, String &error
) {
    LauncherPartitionEntry *existing = launcherPartitionFindByLabel(table, label);
    if (existing) {
        if (existing->isData() && existing->subtype == subtype && existing->size >= requestedSize) {
            entry = *existing;
            return true;
        }
        error = String("Partition ") + label + " is too small or incompatible";
        return false;
    }
    return launcherPartitionCreateData(table, subtype, label, requestedSize, &entry, &error);
}

uint32_t launcherAlignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

bool launcherPartitionCreateDataInLargestFreeRange(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, LauncherPartitionEntry &entry,
    String &error
) {
    LauncherPartitionRange best;
    for (const LauncherPartitionRange &range : launcherPartitionFreeRanges(table)) {
        const uint32_t alignedOffset = launcherAlignUp(range.offset, LAUNCHER_FLASH_SECTOR_SIZE);
        if (alignedOffset < range.offset || range.size < alignedOffset - range.offset) continue;
        const uint32_t alignedSize =
            (range.size - (alignedOffset - range.offset)) & ~(LAUNCHER_FLASH_SECTOR_SIZE - 1);
        if (alignedSize > best.size) best = {alignedOffset, alignedSize};
    }
    if (best.size == 0) {
        error = "No free partition range large enough";
        return false;
    }

    LauncherPartitionEntry created;
    created.type = 0x01;
    created.subtype = subtype;
    created.offset = best.offset;
    created.size = best.size;
    created.flags = 0;
    memset(created.label, 0, sizeof(created.label));
    strncpy(created.label, label, sizeof(created.label) - 1);
    if (!launcherPartitionAdd(table, created, &error)) return false;
    entry = created;
    return true;
}

String launcherHexSize(uint32_t value) {
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "0x%06lX", static_cast<unsigned long>(value));
    return String(buffer);
}

String launcherHumanSize(uint32_t value) {
    if (value >= 1024 * 1024 && value % (1024 * 1024) == 0) return String(value / (1024 * 1024)) + "MB";
    if (value >= 1024 && value % 1024 == 0) return String(value / 1024) + "KB";
    return String(value) + " bytes";
}

String launcherSizeLabel(uint32_t value) {
    return launcherHexSize(value) + " (" + launcherHumanSize(value) + ")";
}

bool launcherPartitionRemoveEntryByOffset(LauncherPartitionTable &table, uint32_t offset) {
    for (auto it = table.entries.begin(); it != table.entries.end(); ++it) {
        if (it->offset == offset) {
            table.entries.erase(it);
            return true;
        }
    }
    return false;
}

bool launcherPartitionIsReplaceableApp(const LauncherPartitionEntry &entry) {
    if (!entry.isOtaApp()) return false;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->address == entry.offset) return false;
    return true;
}

bool launcherPartitionIsRemovableInstallData(const LauncherPartitionEntry &entry) {
    if (!entry.isData()) return false;
    if (entry.subtype != 0x81 && entry.subtype != 0x82 && entry.subtype != 0x83) return false;
    return strcmp(entry.label, "spiffs") == 0 || strcmp(entry.label, "sys") == 0 ||
           strcmp(entry.label, "vfs") == 0;
}

bool launcherPartitionRemoveInstallDataPartitions(LauncherPartitionTable &table, bool removeSpiffs) {
    bool removed = false;
    for (auto it = table.entries.begin(); it != table.entries.end();) {
        bool removable = launcherPartitionIsRemovableInstallData(*it);
        if (removable && !removeSpiffs && strcmp(it->label, "spiffs") == 0) removable = false;
        if (removable) {
            it = table.entries.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    return removed;
}

bool launcherPartitionAddManualAppEntry(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, uint32_t offset, uint32_t size,
    LauncherPartitionEntry &created, String &error
) {
    LauncherPartitionEntry entry;
    entry.type = 0x00;
    entry.subtype = subtype;
    entry.offset = offset;
    entry.size = launcherAlignUp(size, LAUNCHER_APP_PARTITION_ALIGNMENT);
    entry.flags = 0;
    memset(entry.label, 0, sizeof(entry.label));
    strncpy(entry.label, label, sizeof(entry.label) - 1);
    if (!launcherPartitionAdd(table, entry, &error)) return false;
    created = entry;
    return true;
}

uint32_t launcherPartitionDefaultFatSize(const char *label) {
    if (label && strcmp(label, "sys") == 0) return 0x100000;
    if (label && strcmp(label, "ffat") == 0) return 0x100000;
    return LAUNCHER_DEFAULT_FAT_SIZE;
}

uint32_t launcherPartitionBoundedPayloadSize(
    uint32_t declaredSize, uint32_t requestedCopySize, uint32_t maxSize, uint32_t availableSize
) {
    uint32_t copySize = requestedCopySize > 0 ? requestedCopySize : declaredSize;
    if (maxSize > 0 && copySize > maxSize) copySize = maxSize;
    if (availableSize != UINT32_MAX && copySize > availableSize) copySize = availableSize;
    return copySize;
}

LauncherPartitionPayloadPlan launcherPartitionFatPayloadPlan(
    const char *label, uint32_t declaredSize, uint32_t requestedCopySize, uint32_t availableSize
) {
    LauncherPartitionPayloadPlan plan;
    plan.partitionSize = launcherPartitionDefaultFatSize(label);
    plan.copySize = launcherPartitionBoundedPayloadSize(
        declaredSize, requestedCopySize, plan.partitionSize, availableSize
    );
    return plan;
}

bool launcherPartitionSetOtaBoot(const LauncherPartitionTable &table, uint8_t appSubtype, String *error) {
    const int otaIndex = launcherPartitionOtaIndex(appSubtype);
    if (otaIndex < 0) {
        setError(error, "Target app subtype is not an OTA subtype");
        return false;
    }

    const uint8_t otaAppCount = launcherPartitionCountOtaApps(table);
    if (otaAppCount == 0 || otaIndex >= otaAppCount) {
        setError(error, "Target OTA app does not exist in generated table");
        return false;
    }

    const LauncherPartitionEntry *otadata = launcherPartitionFindOtaData(table);
    if (!otadata) {
        setError(error, "otadata partition not found in generated table");
        return false;
    }
    if (otadata->size < LAUNCHER_FLASH_SECTOR_SIZE * 2) {
        setError(error, "otadata partition is too small");
        return false;
    }

    esp_ota_select_entry_t entries[2];
    memset(entries, 0xFF, sizeof(entries));
    esp_flash_read(nullptr, &entries[0], otadata->offset, sizeof(entries[0]));
    esp_flash_read(nullptr, &entries[1], otadata->offset + LAUNCHER_FLASH_SECTOR_SIZE, sizeof(entries[1]));

    int active = bootloader_common_get_active_otadata(entries);
    int next = 0;
    uint32_t nextSeq = static_cast<uint32_t>(otaIndex) + 1;
    if (active != -1) {
        uint32_t seq = entries[active].ota_seq;
        uint32_t generation = 0;
        while (seq > ((static_cast<uint32_t>(otaIndex) + 1) % otaAppCount) + generation * otaAppCount) {
            generation++;
        }
        nextSeq = ((static_cast<uint32_t>(otaIndex) + 1) % otaAppCount) + generation * otaAppCount;
        next = (~active) & 1;
    }

    esp_ota_select_entry_t selected;
    memset(&selected, 0xFF, sizeof(selected));
    selected.ota_seq = nextSeq;
    selected.ota_state = ESP_OTA_IMG_UNDEFINED;
    selected.crc = bootloader_common_ota_select_crc(&selected);

    const uint32_t sectorOffset = otadata->offset + next * LAUNCHER_FLASH_SECTOR_SIZE;
    esp_err_t err = esp_flash_erase_region(nullptr, sectorOffset, LAUNCHER_FLASH_SECTOR_SIZE);
    if (err != ESP_OK) {
        setError(error, "Could not erase otadata sector");
        return false;
    }
    err = esp_flash_write(nullptr, &selected, sectorOffset, sizeof(selected));
    if (err != ESP_OK) {
        setError(error, "Could not write otadata sector");
        return false;
    }

    return true;
}

bool launcherPartitionClearOtaBoot(const LauncherPartitionTable &table, String *error) {
    const LauncherPartitionEntry *otadata = launcherPartitionFindOtaData(table);
    if (!otadata) {
        setError(error, "otadata partition not found in generated table");
        return false;
    }
    esp_err_t err = esp_flash_erase_region(nullptr, otadata->offset, otadata->size);
    if (err != ESP_OK) {
        setError(error, "Could not erase otadata partition");
        return false;
    }
    return true;
}
