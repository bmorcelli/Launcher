#ifndef LAUNCHER_PARTITION_FS_H
#define LAUNCHER_PARTITION_FS_H

#include "partition_table_model.h"

// Detects the filesystem already written on a data partition (FAT or LittleFS,
// probed straight from flash) and, if recognized, mounts it read/write without
// ever formatting, then opens the on-device file browser (list/details/rename/delete).
void launcherBrowsePartitionFiles(const LauncherPartitionEntry &entry);

#endif
