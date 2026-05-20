#include "app_registry.h"
#include "display.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "settings.h"
#include <esp_flash.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <globals.h>
#include <memory>
#include <nvs.h>
#include <nvs_flash.h>
#include <nvs_handle.hpp>

namespace {
constexpr const char *kNamespace = "l_apps";

std::unique_ptr<nvs::NVSHandle> openNamespace(const char *ns, nvs_open_mode_t mode, esp_err_t &err) {
    auto handle = nvs::open_nvs_handle(ns, mode, &err);
    if (err != ESP_OK) {
        log_i("openNamespace(%s) failed: %d", ns, err);
        return nullptr;
    }
    return handle;
}

String loadAppNameForLabel(const char *label) {
    if (!label || !label[0]) return "";
    esp_err_t err = ESP_OK;
    auto handle = openNamespace(kNamespace, NVS_READONLY, err);
    if (!handle) return "";

    char buffer[32] = {0};
    err = handle->get_string(label, buffer, sizeof(buffer));
    if (err == ESP_ERR_NVS_NOT_FOUND) return "";
    if (err != ESP_OK) {
        launcherConsolePrintf("App registry: read failed label=%s err=%d\n", label, err);
        return "";
    }
    return String(buffer);
}

String shortAppActionName(const String &name, const String &fallback) {
    String value = name.isEmpty() ? fallback : name;
    value.trim();
    int firstSpace = value.indexOf(' ');
    if (firstSpace > 0) value = value.substring(0, firstSpace);
    if (value.isEmpty()) value = fallback;
    return value;
}

String fatKeyForLabel(const char *label) {
    String key = "f_";
    key += label ? label : "";
    if (key.length() > 15) key = key.substring(0, 15);
    return key;
}

std::vector<String> parseFatLabels(const String &stored) {
    std::vector<String> labels;
    int start = 0;
    while (start < static_cast<int>(stored.length())) {
        int comma = stored.indexOf(',', start);
        String label = comma >= 0 ? stored.substring(start, comma) : stored.substring(start);
        label.trim();
        if (!label.isEmpty()) labels.push_back(label);
        if (comma < 0) break;
        start = comma + 1;
    }
    return labels;
}

String encodeFatLabels(const std::vector<String> &labels) {
    String out;
    for (const String &label : labels) {
        if (label.isEmpty()) continue;
        if (!out.isEmpty()) out += ",";
        out += label;
    }
    return out;
}

std::vector<String> loadFatLabelsForLabel(const char *label) {
    std::vector<String> labels;
    if (!label || !label[0]) return labels;

    esp_err_t err = ESP_OK;
    auto handle = openNamespace(kNamespace, NVS_READONLY, err);
    if (!handle) return labels;

    String key = fatKeyForLabel(label);
    char buffer[48] = {0};
    err = handle->get_string(key.c_str(), buffer, sizeof(buffer));
    if (err == ESP_ERR_NVS_NOT_FOUND) return labels;
    if (err != ESP_OK) {
        launcherConsolePrintf("App registry: FAT read failed label=%s err=%d\n", label, err);
        return labels;
    }
    return parseFatLabels(String(buffer));
}

bool saveAppNameForLabel(const char *label, const String &name) {
    if (!label || !label[0]) return false;
    esp_err_t err = ESP_OK;
    auto handle = openNamespace(kNamespace, NVS_READWRITE, err);
    if (!handle) return false;

    String storedName = name;
    storedName.trim();
    if (storedName.length() > 20) storedName = storedName.substring(0, 20);

    err = handle->set_string(label, storedName.c_str());
    if (err == ESP_OK) err = handle->commit();
    if (err != ESP_OK) { launcherConsolePrintf("App registry: save failed label=%s err=%d\n", label, err); }
    return err == ESP_OK;
}

bool saveFatLabelsForLabel(const char *label, const std::vector<String> &fatLabels) {
    if (!label || !label[0]) return false;
    esp_err_t err = ESP_OK;
    auto handle = openNamespace(kNamespace, NVS_READWRITE, err);
    if (!handle) return false;

    String key = fatKeyForLabel(label);
    String stored = encodeFatLabels(fatLabels);
    if (stored.isEmpty()) {
        err = handle->erase_item(key.c_str());
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else {
        err = handle->set_string(key.c_str(), stored.c_str());
    }
    if (err == ESP_OK) err = handle->commit();
    if (err != ESP_OK) {
        launcherConsolePrintf("App registry: FAT save failed label=%s err=%d\n", label, err);
    }
    return err == ESP_OK;
}

bool confirmAppDelete(const String &title) {
    bool confirmed = false;
    std::vector<Option> confirmOptions = {
        {"Delete", [&]() { confirmed = true; } },
        {"Cancel", [&]() { confirmed = false; }},
    };
    displayRedStripe(title);
    loopOptions(confirmOptions);
    return confirmed;
}

bool isBootableOtaEntry(const LauncherPartitionEntry &entry) {
    if (!entry.isOtaApp()) return false;
    uint8_t firstByte = 0;
    return esp_flash_read(nullptr, &firstByte, entry.offset, 1) == ESP_OK &&
           firstByte == ESP_IMAGE_HEADER_MAGIC;
}

void normalizeOtaSubtypes(LauncherPartitionTable &table) {
    uint8_t nextSubtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
    for (LauncherPartitionEntry &entry : table.entries) {
        if (!entry.isOtaApp()) continue;
        entry.subtype = nextSubtype++;
    }
}
} // namespace

std::vector<LauncherAppMetadata> launcherLoadAppRegistry() {
    std::vector<LauncherAppMetadata> apps;
    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) return apps;

    for (const LauncherPartitionEntry &entry : table.entries) {
        if (!isBootableOtaEntry(entry)) continue;
        LauncherAppMetadata app;
        app.label = String(entry.label);
        app.name = loadAppNameForLabel(entry.label);
        app.fatLabels = loadFatLabelsForLabel(entry.label);
        if (!app.name.isEmpty()) apps.push_back(app);
    }
    return apps;
}

bool launcherSaveAppMetadata(const LauncherAppMetadata &app) {
    if (app.label.isEmpty()) return false;

    bool saved = saveAppNameForLabel(app.label.c_str(), app.name);
    if (saved) saved = saveFatLabelsForLabel(app.label.c_str(), app.fatLabels);
    launcherConsolePrintf(
        "App registry: save label=%s name=%s fat=%s ok=%d\n",
        app.label.c_str(),
        app.name.c_str(),
        encodeFatLabels(app.fatLabels).c_str(),
        saved
    );
    return saved;
}

bool launcherRemoveAppMetadata(const char *label) {
    if (!label || !label[0]) return false;

    esp_err_t err = ESP_OK;
    auto handle = openNamespace(kNamespace, NVS_READWRITE, err);
    if (!handle) return false;
    err = handle->erase_item(label);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) {
        esp_err_t fatErr = handle->erase_item(fatKeyForLabel(label).c_str());
        if (fatErr != ESP_OK && fatErr != ESP_ERR_NVS_NOT_FOUND) err = fatErr;
    }
    if (err == ESP_OK) err = handle->commit();
    if (err != ESP_OK) { launcherConsolePrintf("App registry: remove failed label=%s err=%d\n", label, err); }
    return err == ESP_OK;
}

std::vector<String> launcherAppFatLabelsForLabel(const char *label) { return loadFatLabelsForLabel(label); }

String launcherAppDisplayNameForLabel(const char *label) {
    if (!label) return "";
    String name = loadAppNameForLabel(label);
    if (!name.isEmpty()) return name;
    return String(label);
}

String launcherSelectedBootAppName() {
    std::vector<LauncherAppMetadata> apps = launcherLoadAppRegistry();

    const esp_partition_t *bootPartition = esp_ota_get_boot_partition();
    if (bootPartition && bootPartition->type == ESP_PARTITION_TYPE_APP &&
        bootPartition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        for (const LauncherAppMetadata &app : apps) {
            if (app.label == String(bootPartition->label)) {
                return app.name.isEmpty() ? app.label : app.name;
            }
        }
    }

    if (apps.size() == 1) return apps[0].name;
    return "";
}

bool launcherBootInstalledAppOrShowMenu() {
    if (!bootToApp) return false;

    std::vector<LauncherAppMetadata> apps = launcherListInstalledApps();
    if (apps.empty()) return false;

    if (apps.size() == 1) return launcherBootAppByLabel(apps[0].label.c_str());

    std::vector<Option> bootOptions;
    bool started = false;
    for (const LauncherAppMetadata &app : apps) {
        String label = app.label;
        String title = app.name.isEmpty() ? app.label : app.name;
        bootOptions.push_back({title, [label, &started]() {
                                   started = launcherBootAppByLabel(label.c_str());
                               }});
    }
    bootOptions.push_back({"Launcher", [&started]() { started = false; }});

    loopOptions(bootOptions);
    return started;
}

String launcherAppNameFromFile(const String &source) {
    String fileName = source;

    int query = fileName.indexOf('?');
    if (query >= 0) fileName = fileName.substring(0, query);
    int fragment = fileName.indexOf('#');
    if (fragment >= 0) fileName = fileName.substring(0, fragment);

    int slash = fileName.lastIndexOf('/');
    int backslash = fileName.lastIndexOf('\\');
    int separator = slash > backslash ? slash : backslash;
    if (separator >= 0) fileName = fileName.substring(separator + 1);

    int dot = fileName.lastIndexOf('.');
    if (dot > 0) fileName = fileName.substring(0, dot);

    fileName.trim();
    if (fileName.length() > 20) fileName = fileName.substring(0, 20);
    return fileName;
}

std::vector<LauncherAppMetadata> launcherListInstalledApps() {
    std::vector<LauncherAppMetadata> apps;
    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) return apps;

    for (const LauncherPartitionEntry &entry : table.entries) {
        if (!isBootableOtaEntry(entry)) continue;

        LauncherAppMetadata app;
        app.label = String(entry.label);
        app.name = loadAppNameForLabel(entry.label);
        if (app.name.isEmpty()) app.name = app.label;
        launcherConsolePrintf("App menu item: label=%s name=%s\n", app.label.c_str(), app.name.c_str());
        apps.push_back(app);
    }
    return apps;
}

bool launcherBootAppByLabel(const char *label) {
    if (!label || !label[0]) {
        displayRedStripe("App not found");
        launcherDelayMs(2000);
        return false;
    }

    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) {
        displayRedStripe(error.length() ? error : "Partition read failed");
        launcherDelayMs(2000);
        return false;
    }

    const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, label);
    if (!entry || !entry->isOtaApp()) {
        displayRedStripe("App not found");
        launcherDelayMs(2000);
        return false;
    }

    if (!launcherPartitionSetOtaBoot(table, entry->subtype, &error)) {
        displayRedStripe(error.length() ? error : "Boot set failed");
        launcherDelayMs(2500);
        return false;
    }

    lastInstalledApp = launcherAppDisplayNameForLabel(label);
    saveIntoNVS();

    FREE_TFT
    reboot();
    return true;
}

bool launcherDeleteAppByLabel(const char *label) {
    if (!label || !label[0]) {
        displayRedStripe("App not found");
        launcherDelayMs(2000);
        return false;
    }

    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) {
        displayRedStripe(error.length() ? error : "Partition read failed");
        launcherDelayMs(2500);
        return false;
    }

    int appIndex = -1;
    LauncherPartitionEntry appEntry;
    for (size_t i = 0; i < table.entries.size(); ++i) {
        if (strcmp(table.entries[i].label, label) == 0 && table.entries[i].isOtaApp()) {
            appIndex = static_cast<int>(i);
            appEntry = table.entries[i];
            break;
        }
    }
    if (appIndex < 0) {
        displayRedStripe("App not found");
        launcherDelayMs(2000);
        return false;
    }

    String appName = launcherAppDisplayNameForLabel(label);
    std::vector<String> linkedFatLabels = launcherAppFatLabelsForLabel(label);
    if (!confirmAppDelete(
            linkedFatLabels.empty() ? String("Delete ") + appName + "?"
                                    : String("Delete ") + appName + " + FAT?"
        ))
        return false;

    LauncherPartitionTable edited = table;
    edited.entries.erase(edited.entries.begin() + appIndex);
    std::vector<LauncherPartitionEntry> removedEntries;
    removedEntries.push_back(appEntry);
    for (const String &fatLabel : linkedFatLabels) {
        for (size_t i = 0; i < edited.entries.size(); ++i) {
            LauncherPartitionEntry &entry = edited.entries[i];
            if (entry.type == ESP_PARTITION_TYPE_DATA && entry.subtype == ESP_PARTITION_SUBTYPE_DATA_FAT &&
                fatLabel == String(entry.label)) {
                removedEntries.push_back(entry);
                edited.entries.erase(edited.entries.begin() + i);
                break;
            }
        }
    }
    normalizeOtaSubtypes(edited);
    if (!launcherPartitionCompact(edited, &error)) {
        displayRedStripe(error.length() ? error : "Compact failed");
        launcherDelayMs(2500);
        return false;
    }
    if (!launcherPartitionValidate(edited, &error)) {
        displayRedStripe(error.length() ? error : "Invalid table");
        launcherDelayMs(2500);
        return false;
    }

    displayRedStripe("Clearing boot");
    if (!launcherPartitionClearOtaBoot(table, &error)) {
        displayRedStripe(error.length() ? error : "Boot clear failed");
        launcherDelayMs(2500);
        return false;
    }

    displayRedStripe("Removing firmware");
    for (const LauncherPartitionEntry &removed : removedEntries) {
        esp_err_t err = esp_flash_erase_region(nullptr, removed.offset, removed.size);
        if (err != ESP_OK) {
            launcherConsolePrintf(
                "Partition erase failed label=%s offset=0x%08X size=0x%08X err=%d\n",
                removed.label,
                removed.offset,
                removed.size,
                err
            );
            displayRedStripe("Erase failed");
            launcherDelayMs(2500);
            return false;
        }
    }

    displayRedStripe("Optimizing flash");
    if (!launcherPartitionMigrateMovedData(table, edited, &error)) {
        displayRedStripe(error.length() ? error : "Move failed");
        launcherDelayMs(2500);
        return false;
    }

    displayRedStripe("Writing table");
    if (!launcherPartitionWriteGeneratedTable(edited, &error)) {
        displayRedStripe(error.length() ? error : "Write failed");
        launcherDelayMs(2500);
        return false;
    }

    launcherRemoveAppMetadata(label);
    displayRedStripe("Restart needed");
    launcherDelayMs(1500);
    FREE_TFT
    reboot();
    return true;
}

bool launcherRenameAppByLabel(const char *label) {
    if (!label || !label[0]) {
        displayRedStripe("App not found");
        launcherDelayMs(2000);
        return false;
    }

    String appLabel = String(label);
    String currentName = loadAppNameForLabel(label);
    if (currentName.isEmpty()) currentName = appLabel;

    String newName = keyboard(currentName, 20, "App Name:");
    newName.trim();
    if (newName.isEmpty() || newName == String(KEY_ESCAPE) || newName == currentName) { return false; }

    if (!saveAppNameForLabel(label, newName)) {
        displayRedStripe("Rename failed");
        launcherDelayMs(2000);
        return false;
    }

    displayRedStripe("App renamed");
    launcherDelayMs(1000);
    return true;
}

void launcherShowAppActions(const char *label) {
    if (!label || !label[0]) {
        displayRedStripe("App not found");
        launcherDelayMs(2000);
        return;
    }

    String appLabel = String(label);
    String appName = shortAppActionName(loadAppNameForLabel(label), appLabel);
    std::vector<Option> appOptions = {
        {String("Launch ") + appName, [appLabel]() { launcherBootAppByLabel(appLabel.c_str()); }  },
        {"Rename App",                [appLabel]() { launcherRenameAppByLabel(appLabel.c_str()); }},
        {String("Delete ") + appName, [appLabel]() { launcherDeleteAppByLabel(appLabel.c_str()); }},
        {"Cancel",                    []() {}                                                     },
    };
    loopOptions(appOptions);
}

void launcherShowAppLauncher() {
    std::vector<Option> appOptions;
    for (const LauncherAppMetadata &app : launcherListInstalledApps()) {
        String label = app.label;
        String title = app.name.isEmpty() ? app.label : app.name;
        appOptions.push_back({title, [label]() { launcherShowAppActions(label.c_str()); }});
    }
    appOptions.push_back({"Cancel", []() {}});

    if (appOptions.size() <= 1) {
        displayRedStripe("No apps found");
        launcherDelayMs(2000);
        return;
    }
    loopOptions(appOptions);
}
