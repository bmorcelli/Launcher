#ifndef LAUNCHER_PARTITION_INSTALL_LAYOUT_H
#define LAUNCHER_PARTITION_INSTALL_LAYOUT_H

#include "partition_table_model.h"
#include "pre_compiler.h"

constexpr uint32_t LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE = 0xFFFFFFFF;

struct LauncherInstallFatPartition {
    String label;
    uint32_t sourceOffset = 0;
    uint32_t partitionSize = 0;
    uint32_t copySize = 0;
    LauncherPartitionEntry entry;
    bool hasEntry = false;
};

bool launcherPrepareInstallDataPartitions(
    LauncherPartitionTable &table, bool spiffs, uint32_t spiffsSize, LauncherPartitionEntry &spiffsEntry,
    bool &hasSpiffsEntry, std::vector<LauncherInstallFatPartition> &fatPartitions, String &error
);

bool launcherSelectInstallLayout(
    LauncherPartitionTable &table, size_t updateSize, const String &defaultLabel, bool spiffs,
    uint32_t spiffsSize, std::vector<LauncherInstallFatPartition> &fatPartitions,
    LauncherPartitionEntry &appEntry, LauncherPartitionEntry &spiffsEntry, bool &hasSpiffsEntry, String &error
);

#endif
