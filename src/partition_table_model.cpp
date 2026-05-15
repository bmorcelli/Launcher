#include "partition_table_model.h"

#include <algorithm>
#include <bootloader_common.h>
#include <cstring>
#include <esp_flash.h>
#include <esp_flash_partitions.h>
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

bool aligned(uint32_t value, uint32_t alignment) {
    return alignment > 0 && (value % alignment) == 0;
}

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

bool isProtectedDataSubtype(uint8_t subtype) {
    return subtype == kSubtypeDataOta || subtype == kSubtypeDataNvs || subtype == kSubtypeDataRf ||
           subtype == kSubtypeDataWifi || subtype == kSubtypeDataEfuse;
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
}

bool LauncherPartitionEntry::isApp() const {
    return type == kTypeApp;
}

bool LauncherPartitionEntry::isData() const {
    return type == kTypeData;
}

bool LauncherPartitionEntry::isOtaApp() const {
    return isApp() && subtype >= kSubtypeOtaFlag && subtype <= (kSubtypeOtaFlag | kSubtypeOtaMask);
}

bool LauncherPartitionEntry::isFactoryOrTestApp() const {
    return isApp() && (subtype == kSubtypeFactory || subtype == kSubtypeTest);
}

bool launcherPartitionReadCurrent(LauncherPartitionTable &table, String *error) {
    uint8_t raw[LAUNCHER_PARTITION_TABLE_SIZE];
    esp_err_t err = esp_flash_read(nullptr, raw, LAUNCHER_PARTITION_TABLE_OFFSET, sizeof(raw));
    if (err != ESP_OK) {
        setError(error, "Could not read partition table from flash");
        return false;
    }
    return launcherPartitionParse(raw, sizeof(raw), table, error);
}

bool launcherPartitionParse(
    const uint8_t *data, size_t size, LauncherPartitionTable &table, String *error
) {
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

        if (magic == 0xFFFF || magic == 0x0000) {
            return launcherPartitionValidate(table, error);
        }

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
        if (!aligned(entry.offset, LAUNCHER_FLASH_SECTOR_SIZE) || !aligned(entry.size, LAUNCHER_FLASH_SECTOR_SIZE)) {
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
        if (entry.isData() && isProtectedDataSubtype(entry.subtype) && entry.size < LAUNCHER_FLASH_SECTOR_SIZE) {
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

bool launcherPartitionWriteGeneratedTable(const LauncherPartitionTable &table, String *error) {
    uint8_t raw[LAUNCHER_PARTITION_TABLE_SIZE];
    if (!launcherPartitionBuild(table, raw, sizeof(raw), error)) return false;

    esp_err_t err = esp_flash_erase_region(
        nullptr, LAUNCHER_PARTITION_TABLE_OFFSET, LAUNCHER_PARTITION_TABLE_SIZE
    );
    if (err != ESP_OK) {
        setError(error, "Could not erase partition table sector");
        return false;
    }

    err = esp_flash_write(nullptr, raw, LAUNCHER_PARTITION_TABLE_OFFSET, sizeof(raw));
    if (err != ESP_OK) {
        setError(error, "Could not write generated partition table");
        return false;
    }

    return true;
}

LauncherPartitionEntry *launcherPartitionFindByLabel(LauncherPartitionTable &table, const char *label) {
    if (!label) return nullptr;
    for (LauncherPartitionEntry &entry : table.entries) {
        if (strncmp(entry.label, label, 16) == 0) return &entry;
    }
    return nullptr;
}

const LauncherPartitionEntry *launcherPartitionFindByLabel(
    const LauncherPartitionTable &table, const char *label
) {
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

const LauncherPartitionEntry *launcherPartitionFindAppBySubtype(
    const LauncherPartitionTable &table, uint8_t subtype
) {
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
    std::sort(entries.begin(), entries.end(), [](const LauncherPartitionEntry &a, const LauncherPartitionEntry &b) {
        return a.offset < b.offset;
    });

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
    const LauncherPartitionTable &table,
    uint32_t requiredSize,
    uint32_t alignment,
    LauncherPartitionRange &range,
    String *error
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
    std::sort(table.entries.begin(), table.entries.end(), [](const LauncherPartitionEntry &a, const LauncherPartitionEntry &b) {
        return a.offset < b.offset;
    });
    return true;
}

bool launcherPartitionCreateOtaApp(
    LauncherPartitionTable &table,
    uint32_t imageSize,
    const char *label,
    LauncherPartitionEntry *created,
    String *error
) {
    int subtype = launcherPartitionNextOtaSubtype(table);
    if (subtype < 0) {
        setError(error, "No OTA app subtype available");
        return false;
    }

    uint32_t partitionSize = alignUp(imageSize, LAUNCHER_FLASH_SECTOR_SIZE);
    LauncherPartitionRange range;
    if (!launcherPartitionFindFreeRange(table, partitionSize, 0x10000, range, error)) return false;

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
    LauncherPartitionTable &table,
    uint8_t subtype,
    const char *label,
    uint32_t size,
    LauncherPartitionEntry *created,
    String *error
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

uint32_t launcherPartitionDefaultFatSize(const char *label) {
    if (label && strcmp(label, "sys") == 0) return 0x100000;
    if (label && strcmp(label, "vfs") == 0) return 0x70000;
    return 0x80000;
}

bool launcherPartitionSetOtaBoot(
    const LauncherPartitionTable &table, uint8_t appSubtype, String *error
) {
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
