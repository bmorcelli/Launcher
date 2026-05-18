#ifndef LAUNCHER_APP_REGISTRY_H
#define LAUNCHER_APP_REGISTRY_H

#include "partition_table_model.h"
#include <Arduino.h>
#include <vector>

struct LauncherAppMetadata {
    String name;
    String label;
};

std::vector<LauncherAppMetadata> launcherLoadAppRegistry();
std::vector<LauncherAppMetadata> launcherListInstalledApps();
bool launcherSaveAppMetadata(const LauncherAppMetadata &app);
bool launcherRemoveAppMetadata(const char *label);
void launcherShowAppLauncher();
void launcherShowAppActions(const char *label);
String launcherAppDisplayNameForLabel(const char *label);
bool launcherBootAppByLabel(const char *label);
bool launcherDeleteAppByLabel(const char *label);
String launcherAppNameFromFile(const String &source);

#endif
