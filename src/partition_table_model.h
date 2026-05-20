#ifndef LAUNCHER_PARTITION_TABLE_MODEL_H
#define LAUNCHER_PARTITION_TABLE_MODEL_H

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <vector>

constexpr uint32_t LAUNCHER_PARTITION_TABLE_OFFSET = 0x8000;
constexpr uint32_t LAUNCHER_PARTITION_TABLE_SIZE = 0x1000;
constexpr uint32_t LAUNCHER_FLASH_SECTOR_SIZE = 0x1000;
constexpr uint32_t LAUNCHER_APP_PARTITION_ALIGNMENT = 0x10000;
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

struct LauncherPartitionPayloadPlan {
    uint32_t partitionSize = 0;
    uint32_t copySize = 0;
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
uint32_t launcherPartitionAlignment(uint8_t type, uint8_t subtype);
bool launcherPartitionCompact(LauncherPartitionTable &table, String *error = nullptr);
bool launcherPartitionMigrateMovedData(
    const LauncherPartitionTable &currentTable, const LauncherPartitionTable &targetTable,
    String *error = nullptr
);

LauncherPartitionEntry *launcherPartitionFindByLabel(LauncherPartitionTable &table, const char *label);
const LauncherPartitionEntry *
launcherPartitionFindByLabel(const LauncherPartitionTable &table, const char *label);
LauncherPartitionEntry *launcherPartitionFindAppBySubtype(LauncherPartitionTable &table, uint8_t subtype);
const LauncherPartitionEntry *
launcherPartitionFindAppBySubtype(const LauncherPartitionTable &table, uint8_t subtype);
const LauncherPartitionEntry *launcherPartitionFindOtaData(const LauncherPartitionTable &table);
uint8_t launcherPartitionCountOtaApps(const LauncherPartitionTable &table);
int launcherPartitionOtaIndex(uint8_t subtype);
int launcherPartitionNextOtaSubtype(const LauncherPartitionTable &table);
std::vector<LauncherPartitionRange> launcherPartitionFreeRanges(const LauncherPartitionTable &table);
bool launcherPartitionFindFreeRange(
    const LauncherPartitionTable &table, uint32_t requiredSize, uint32_t alignment,
    LauncherPartitionRange &range, String *error = nullptr
);
bool launcherPartitionAdd(
    LauncherPartitionTable &table, const LauncherPartitionEntry &entry, String *error = nullptr
);
bool launcherPartitionCreateOtaApp(
    LauncherPartitionTable &table, uint32_t imageSize, const char *label,
    LauncherPartitionEntry *created = nullptr, String *error = nullptr
);
bool launcherPartitionCreateData(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, uint32_t size,
    LauncherPartitionEntry *created = nullptr, String *error = nullptr
);
String launcherPartitionSanitizedAppLabelBase(const String &name);
bool launcherPartitionLabelExists(const LauncherPartitionTable &table, const String &label);
String launcherPartitionNextAppLabel(const LauncherPartitionTable &table, const String &installedName);
bool launcherPartitionRenameEntryByOffset(
    LauncherPartitionTable &table, uint32_t offset, const String &label
);
bool launcherPartitionFindOrCreateData(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, uint32_t requestedSize,
    LauncherPartitionEntry &entry, String &error
);
uint32_t launcherAlignUp(uint32_t value, uint32_t alignment);
bool launcherPartitionCreateDataInLargestFreeRange(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, LauncherPartitionEntry &entry,
    String &error
);
String launcherHexSize(uint32_t value);
String launcherHumanSize(uint32_t value);
String launcherSizeLabel(uint32_t value);
bool launcherPartitionRemoveEntryByOffset(LauncherPartitionTable &table, uint32_t offset);
bool launcherPartitionIsReplaceableApp(const LauncherPartitionEntry &entry);
bool launcherPartitionIsRemovableInstallData(const LauncherPartitionEntry &entry);
bool launcherPartitionRemoveInstallDataPartitions(LauncherPartitionTable &table, bool removeSpiffs);
bool launcherPartitionAddManualAppEntry(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, uint32_t offset, uint32_t size,
    LauncherPartitionEntry &created, String &error
);
uint32_t launcherPartitionDefaultFatSize(const char *label);
uint32_t launcherPartitionBoundedPayloadSize(
    uint32_t declaredSize, uint32_t requestedCopySize, uint32_t maxSize, uint32_t availableSize = UINT32_MAX
);
LauncherPartitionPayloadPlan launcherPartitionFatPayloadPlan(
    const char *label, uint32_t declaredSize, uint32_t requestedCopySize = 0,
    uint32_t availableSize = UINT32_MAX
);
bool launcherPartitionSetOtaBoot(
    const LauncherPartitionTable &table, uint8_t appSubtype, String *error = nullptr
);
bool launcherPartitionClearOtaBoot(const LauncherPartitionTable &table, String *error = nullptr);

#endif
