#ifndef LAUNCHER_LITTLEFS_PATCH_H
#define LAUNCHER_LITTLEFS_PATCH_H

#include "partition_table_model.h"

bool launcherPatchReducedLittlefsSuperblocks(
    uint32_t address, uint32_t partitionSize, String *error = nullptr, bool *patched = nullptr
);

bool launcherPartitionLooksLikeLittlefs(uint32_t address, uint32_t partitionSize);

inline bool launcherPatchReducedLittlefsSuperblocks(
    const LauncherPartitionEntry &entry, String *error = nullptr, bool *patched = nullptr
) {
    return launcherPatchReducedLittlefsSuperblocks(entry.offset, entry.size, error, patched);
}

#endif
