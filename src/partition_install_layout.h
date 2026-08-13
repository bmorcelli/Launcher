#ifndef LAUNCHER_PARTITION_INSTALL_LAYOUT_H
#define LAUNCHER_PARTITION_INSTALL_LAYOUT_H

#include "partition_table_model.h"
#include "pre_compiler.h"

constexpr uint32_t LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE = 0xFFFFFFFF;

struct LauncherInstallDataPartition {
    uint8_t subtype = 0x82; // 0x81=FAT, 0x82=SPIFFS, 0x83=LittleFS
    String label;
    uint32_t sourceOffset = 0;
    uint32_t partitionSize = 0;
    uint32_t copySize = 0;
    LauncherPartitionEntry entry;
    bool hasEntry = false;
    // HTTP source for this partition's payload. Empty means it lives inside the app
    // image (same URL as the firmware); set when the data ships as a separate file
    // (manifest "source" points to a distinct entry of install.sources).
    String sourceUrl;
};

bool launcherPrepareInstallDataPartitions(
    LauncherPartitionTable &table, std::vector<LauncherInstallDataPartition> &dataPartitions, String &error
);

bool launcherSelectInstallLayout(
    LauncherPartitionTable &table, size_t updateSize, const String &defaultLabel,
    std::vector<LauncherInstallDataPartition> &dataPartitions, LauncherPartitionEntry &appEntry, String &error
);

#endif
