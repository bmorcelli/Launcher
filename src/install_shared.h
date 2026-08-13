#ifndef LAUNCHER_INSTALL_SHARED_H
#define LAUNCHER_INSTALL_SHARED_H

#include "partition_table_model.h"
#include <vector>

const char *launcherInstallDefaultDataLabel(uint8_t subtype);

String launcherInstallAppDisplayName(const String &sourceName, const String &fallbackName = "");
String launcherInstallNextAppLabel(
    const LauncherPartitionTable &table, const String &sourceName, const String &preferredName = "",
    const char *emptyFallback = nullptr
);

void launcherSaveInstalledAppMetadata(
    const LauncherPartitionTable &table, const LauncherPartitionEntry &appEntry, const String &sourceName,
    const String &preferredName, const std::vector<String> &fatLabels, const String &spiffsLabel = ""
);

#endif
