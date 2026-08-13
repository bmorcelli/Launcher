#ifndef LAUNCHER_APP_REGISTRY_H
#define LAUNCHER_APP_REGISTRY_H

#include "partition_table_model.h"
#include <Arduino.h>
#include <vector>

struct LauncherAppMetadata {
    String name;
    String label;
    std::vector<String> fatLabels;
    String spiffsLabel;
    String appNum; // CRC32 8-char hash of SD filepath (e.g. "a1b2c3d4")
};

std::vector<LauncherAppMetadata> launcherLoadAppRegistry();
std::vector<LauncherAppMetadata> launcherListInstalledApps();
bool launcherSaveAppMetadata(const LauncherAppMetadata &app);
bool launcherRemoveAppMetadata(const char *label);
std::vector<String> launcherAppFatLabelsForLabel(const char *label);
String launcherAppSpiffsLabelForLabel(const char *label);
void launcherShowAppLauncher();
void launcherShowAppActions(const char *label);
String launcherAppDisplayNameForLabel(const char *label);
String launcherSelectedBootAppName();
bool launcherBootCurrentApp();
bool launcherBootInstalledAppOrShowMenu();
bool launcherBootAppByLabel(const char *label);
bool launcherDeleteAppByLabel(const char *label);
bool launcherRenameAppByLabel(const char *label);
String launcherAppNameFromFile(const String &source);
bool launcherClearAppRegistry();

#endif
