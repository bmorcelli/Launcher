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

// "Update <app>" target: the installed app partition carrying the label an install of
// `displayName` would create, when launcherSelectUpdateLayout can fit this image into it.
// Returns its label, or "" if none.
String launcherInstallUpdateTarget(
    const String &displayName, size_t updateSize,
    const std::vector<LauncherInstallDataPartition> &dataPartitions
);

// Layout for "Update <app>": the app goes back into its own `label` partition when it fits.
// Otherwise that partition is dropped and, if growing into free space isn't enough, the
// flash is compacted (other apps/data moved toward the start) to make room for it.
bool launcherSelectUpdateLayout(
    LauncherPartitionTable &table, size_t updateSize, const String &label,
    std::vector<LauncherInstallDataPartition> &dataPartitions, LauncherPartitionEntry &appEntry, String &error
);

// Moves the partitions an update layout relocated toward the start of flash and writes the
// new table right away, so the other apps stay bootable if flashing the update fails.
// No-op when nothing moved. Must run before anything is flashed into `target`.
bool launcherApplyUpdateLayoutMoves(
    const LauncherPartitionTable &target, const LauncherPartitionEntry &appEntry, String &error
);

#endif
