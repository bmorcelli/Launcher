#ifndef LAUNCHER_PARTITION_TABLE_MODEL_H
#define LAUNCHER_PARTITION_TABLE_MODEL_H

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <vector>

constexpr uint32_t LAUNCHER_PARTITION_TABLE_OFFSET = 0x8000;
constexpr uint32_t LAUNCHER_PARTITION_TABLE_SIZE = 0x1000;
constexpr uint32_t LAUNCHER_FLASH_SECTOR_SIZE = 0x1000;
constexpr size_t LAUNCHER_PARTITION_ENTRY_SIZE = 0x20;

struct LauncherPartitionEntry {
    uint8_t type = 0xFF;
    uint8_t subtype = 0xFF;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t flags = 0;
    char label[17] = {0};

    bool isApp() const;
    bool isData() const;
    bool isOtaApp() const;
    bool isFactoryOrTestApp() const;
};

struct LauncherPartitionTable {
    std::vector<LauncherPartitionEntry> entries;
    uint32_t flashSize = 0;
    bool hasMd5 = false;
};

struct LauncherPartitionRange {
    uint32_t offset = 0;
    uint32_t size = 0;
};

bool launcherPartitionReadCurrent(LauncherPartitionTable &table, String *error = nullptr);
bool launcherPartitionParse(
    const uint8_t *data, size_t size, LauncherPartitionTable &table, String *error = nullptr
);
bool launcherPartitionBuild(
    const LauncherPartitionTable &table, uint8_t *out, size_t outSize, String *error = nullptr
);
bool launcherPartitionValidate(const LauncherPartitionTable &table, String *error = nullptr);
bool launcherPartitionWriteGeneratedTable(const LauncherPartitionTable &table, String *error = nullptr);

LauncherPartitionEntry *launcherPartitionFindByLabel(LauncherPartitionTable &table, const char *label);
const LauncherPartitionEntry *launcherPartitionFindByLabel(
    const LauncherPartitionTable &table, const char *label
);
LauncherPartitionEntry *launcherPartitionFindAppBySubtype(LauncherPartitionTable &table, uint8_t subtype);
const LauncherPartitionEntry *launcherPartitionFindAppBySubtype(
    const LauncherPartitionTable &table, uint8_t subtype
);
const LauncherPartitionEntry *launcherPartitionFindOtaData(const LauncherPartitionTable &table);
uint8_t launcherPartitionCountOtaApps(const LauncherPartitionTable &table);
int launcherPartitionOtaIndex(uint8_t subtype);
int launcherPartitionNextOtaSubtype(const LauncherPartitionTable &table);
std::vector<LauncherPartitionRange> launcherPartitionFreeRanges(const LauncherPartitionTable &table);
bool launcherPartitionFindFreeRange(
    const LauncherPartitionTable &table,
    uint32_t requiredSize,
    uint32_t alignment,
    LauncherPartitionRange &range,
    String *error = nullptr
);
bool launcherPartitionAdd(LauncherPartitionTable &table, const LauncherPartitionEntry &entry, String *error = nullptr);
bool launcherPartitionCreateOtaApp(
    LauncherPartitionTable &table,
    uint32_t imageSize,
    const char *label,
    LauncherPartitionEntry *created = nullptr,
    String *error = nullptr
);
bool launcherPartitionCreateData(
    LauncherPartitionTable &table,
    uint8_t subtype,
    const char *label,
    uint32_t size,
    LauncherPartitionEntry *created = nullptr,
    String *error = nullptr
);
uint32_t launcherPartitionDefaultFatSize(const char *label);
bool launcherPartitionSetOtaBoot(
    const LauncherPartitionTable &table, uint8_t appSubtype, String *error = nullptr
);
bool launcherPartitionClearOtaBoot(const LauncherPartitionTable &table, String *error = nullptr);

#endif
