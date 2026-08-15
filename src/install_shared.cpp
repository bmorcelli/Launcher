#include "install_shared.h"
#include "app_registry.h"
#include "backup_manager.h"
#include <globals.h>

const char *launcherInstallDefaultDataLabel(uint8_t subtype) { return subtype == 0x81 ? "vfs" : "spiffs"; }

String launcherInstallAppDisplayName(const String &sourceName, const String &fallbackName) {
    if (!fallbackName.isEmpty()) return fallbackName;
    return launcherAppNameFromFile(sourceName);
}

String launcherInstallNextAppLabel(
    const LauncherPartitionTable &table, const String &sourceName, const String &preferredName,
    const char *emptyFallback
) {
    String labelSource = launcherInstallAppDisplayName(sourceName, preferredName);
    if (labelSource.isEmpty() && emptyFallback != nullptr) labelSource = emptyFallback;
    return launcherPartitionNextAppLabel(table, labelSource);
}

void launcherSaveInstalledAppMetadata(
    const LauncherPartitionTable &table, const LauncherPartitionEntry &appEntry, const String &sourceName,
    const String &preferredName, const std::vector<String> &fatLabels, const String &spiffsLabel
) {
    if (appEntry.offset == 0) {
        lastInstalledApp = launcherInstallAppDisplayName(sourceName, preferredName);
        if (lastInstalledApp.isEmpty()) lastInstalledApp = "WebUI File";
        return;
    }

    String installedLabel = String(appEntry.label);
    launcherPruneAppRegistry(table);

    LauncherAppMetadata metadata;
    metadata.name = launcherInstallAppDisplayName(sourceName, preferredName);
    if (metadata.name.isEmpty()) metadata.name = installedLabel;
    metadata.label = installedLabel;
    metadata.fatLabels = fatLabels;
    metadata.spiffsLabel = spiffsLabel;
    metadata.appNum = generateAppNum(sourceName);
    launcherSaveAppMetadata(metadata);
    lastInstalledApp = metadata.name;
}
