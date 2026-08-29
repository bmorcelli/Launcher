#include "app_registry.h"
#include "backup_manager.h"
#include "ble_bonds.h"
#include "display.h"
#include "idf/launcher_platform.h"
#include "launcher_capabilities.h"
#include "mykeyboard.h"
#include "nvs_helpers.h"
#include "settings.h"
#include "utils.h"
#include <cstring>
#include <esp_flash.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <globals.h>
#include <memory>

namespace {
constexpr const char *kNamespace = "l_apps";
constexpr uint32_t kPaperMonoLauncherAddress = 0x10000;
constexpr uint32_t kPaperMonoLauncherSize = 0x180000;
constexpr uint32_t kPaperMonoPayloadAddress = 0x1A0000;
constexpr uint32_t kPaperMonoPayloadSize = 0xE60000;

String loadAppNameForLabel(const char *label) {
    if (!label || !label[0]) return "";
    return lnvs::getString(kNamespace, label, 31);
}

String shortAppActionName(const String &name, const String &fallback) {
    String value = name.isEmpty() ? fallback : name;
    value.trim();
    int firstSpace = value.indexOf(' ');
    if (firstSpace > 0) value = value.substring(0, firstSpace);
    if (value.isEmpty()) value = fallback;
    return value;
}

String dataKeyForLabel(const char *prefix, const char *label) {
    String key = prefix;
    key += label ? label : "";
    if (key.length() > 15) key = key.substring(0, 15);
    return key;
}

String loadNvsString(const char *key, size_t maxLen) { return lnvs::getString(kNamespace, key, maxLen); }

bool saveNvsString(const char *key, const String &value) {
    lnvs::Handle handle(kNamespace, true);
    if (!handle) return false;

    bool ok = value.isEmpty() ? lnvs::eraseKey(handle.raw(), key)
                              : lnvs::setString(handle.raw(), key, value.c_str());
    if (!ok) launcherConsolePrintf("App registry: write failed key=%s\n", key);
    return ok && handle.commit();
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
    if (!label || !label[0]) return {};
    return parseFatLabels(loadNvsString(dataKeyForLabel("f_", label).c_str(), 47));
}

String loadSpiffsLabelForLabel(const char *label) {
    if (!label || !label[0]) return "";
    return loadNvsString(dataKeyForLabel("s_", label).c_str(), 16);
}

bool saveAppNameForLabel(const char *label, const String &name) {
    if (!label || !label[0]) return false;

    String storedName = name;
    storedName.trim();
    if (storedName.length() > 20) storedName = storedName.substring(0, 20);

    bool ok = lnvs::setString(kNamespace, label, storedName.c_str());
    if (!ok) launcherConsolePrintf("App registry: save failed label=%s\n", label);
    return ok;
}

bool saveFatLabelsForLabel(const char *label, const std::vector<String> &fatLabels) {
    if (!label || !label[0]) return false;
    return saveNvsString(dataKeyForLabel("f_", label).c_str(), encodeFatLabels(fatLabels));
}

bool saveSpiffsLabelForLabel(const char *label, const String &spiffsLabel) {
    if (!label || !label[0]) return false;
    return saveNvsString(dataKeyForLabel("s_", label).c_str(), spiffsLabel);
}

String loadAppNumForLabel(const char *label) {
    if (!label || !label[0]) return "";
    return loadNvsString(dataKeyForLabel("n_", label).c_str(), 8);
}

bool saveAppNumForLabel(const char *label, const String &appNum) {
    if (!label || !label[0]) return false;
    return saveNvsString(dataKeyForLabel("n_", label).c_str(), appNum);
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

bool paperMonoSelectPayloadBoot(const LauncherPartitionTable &table, String *error) {
    if (!launcherAppBootSelectAllowed()) {
        if (error) *error = "PaperMono app boot selection disabled";
        return false;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running || running->type != ESP_PARTITION_TYPE_APP ||
        running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY || strcmp(running->label, "launcher") != 0 ||
        running->address != kPaperMonoLauncherAddress || running->size != kPaperMonoLauncherSize) {
        if (error) *error = "Running app is not the PaperMono factory Launcher";
        return false;
    }

    const LauncherPartitionEntry *target = launcherPartitionFindByLabel(table, "payload");
    if (!target || target->type != ESP_PARTITION_TYPE_APP ||
        target->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 || target->offset != kPaperMonoPayloadAddress ||
        target->size != kPaperMonoPayloadSize) {
        if (error) *error = "PaperMono payload partition does not match the approved layout";
        return false;
    }
    if (!isBootableOtaEntry(*target)) {
        if (error) *error = "PaperMono payload image is not bootable";
        return false;
    }

    const esp_partition_t *payload =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "payload");
    if (!payload || payload->address != kPaperMonoPayloadAddress || payload->size != kPaperMonoPayloadSize ||
        payload->type != ESP_PARTITION_TYPE_APP || payload->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
        strcmp(payload->label, "payload") != 0 || payload->address == running->address) {
        if (error) *error = "PaperMono payload target is not the approved OTA_0 partition";
        return false;
    }

    if (esp_ota_set_boot_partition(payload) != ESP_OK) {
        if (error) *error = "Could not select the PaperMono payload boot partition";
        return false;
    }

    const esp_partition_t *selected = esp_ota_get_boot_partition();
    if (!selected || selected->address != payload->address || selected->size != payload->size ||
        selected->type != payload->type || selected->subtype != payload->subtype ||
        strcmp(selected->label, payload->label) != 0) {
        if (error) *error = "PaperMono payload boot selection verification failed";
        return false;
    }
    return true;
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
        app.spiffsLabel = loadSpiffsLabelForLabel(entry.label);
        app.appNum = loadAppNumForLabel(entry.label);
        if (!app.name.isEmpty()) apps.push_back(app);
    }
    return apps;
}

bool launcherClearAppRegistry() { return lnvs::eraseNamespace(kNamespace); }

bool launcherPruneAppRegistry(const LauncherPartitionTable &table) {
    std::vector<String> storedKeys = lnvs::keys(kNamespace);
    if (storedKeys.empty()) return true;

    lnvs::Handle handle(kNamespace, true);
    if (!handle) return false;

    bool changed = false;
    for (const String &storedKey : storedKeys) {
        const char *key = storedKey.c_str();

        bool remove = false;
        const bool linkedKey = key[1] == '_' && (key[0] == 'f' || key[0] == 's' || key[0] == 'n');
        if (linkedKey) {
            remove = true;
            for (const LauncherPartitionEntry &entry : table.entries) {
                if (!entry.isOtaApp()) continue;
                char expected[16] = {key[0], '_', 0};
                strncpy(expected + 2, entry.label, sizeof(expected) - 3);
                if (strcmp(expected, key) == 0) {
                    remove = false;
                    break;
                }
            }

            if (!remove && key[0] != 'n') {
                String value = lnvs::getString(handle.raw(), key, key[0] == 's' ? 16 : 63);
                if (value.isEmpty()) continue; // unreadable or gone: leave it alone

                const char *labelStart = value.c_str();
                do {
                    char label[16] = {0};
                    const char *labelEnd = strchr(labelStart, ',');
                    size_t len = labelEnd ? static_cast<size_t>(labelEnd - labelStart) : strlen(labelStart);
                    if (len >= sizeof(label)) len = sizeof(label) - 1;
                    memcpy(label, labelStart, len);

                    bool dataExists = false;
                    for (const LauncherPartitionEntry &entry : table.entries) {
                        if (entry.isData() && strcmp(entry.label, label) == 0) {
                            dataExists = true;
                            break;
                        }
                    }
                    if (!dataExists) remove = true;
                    labelStart = labelEnd ? labelEnd + 1 : nullptr;
                } while (!remove && key[0] == 'f' && labelStart && *labelStart);
            }
        } else {
            remove = true;
            for (const LauncherPartitionEntry &entry : table.entries) {
                if (entry.isOtaApp() && strcmp(entry.label, key) == 0) {
                    remove = false;
                    break;
                }
            }
        }

        if (remove) {
            if (!lnvs::eraseKey(handle.raw(), key)) return false;
            changed = true;
        }
    }

    return changed ? handle.commit() : true;
}

bool launcherSaveAppMetadata(const LauncherAppMetadata &app) {
    if (app.label.isEmpty()) return false;

    bool saved = saveAppNameForLabel(app.label.c_str(), app.name);
    if (saved) saved = saveFatLabelsForLabel(app.label.c_str(), app.fatLabels);
    if (saved) saved = saveSpiffsLabelForLabel(app.label.c_str(), app.spiffsLabel);
    if (saved && !app.appNum.isEmpty()) saved = saveAppNumForLabel(app.label.c_str(), app.appNum);
    launcherConsolePrintf(
        "App registry: save label=%s name=%s fat=%s spiffs=%s appNum=%s ok=%d\n",
        app.label.c_str(),
        app.name.c_str(),
        encodeFatLabels(app.fatLabels).c_str(),
        app.spiffsLabel.c_str(),
        app.appNum.c_str(),
        saved
    );
    return saved;
}

bool launcherRemoveAppMetadata(const char *label) {
    if (!label || !label[0]) return false;

    lnvs::Handle handle(kNamespace, true);
    if (!handle) return false;

    bool ok = lnvs::eraseKey(handle.raw(), label);
    for (const char *prefix : {"f_", "s_", "n_"}) {
        if (!ok) break;
        ok = lnvs::eraseKey(handle.raw(), dataKeyForLabel(prefix, label).c_str());
    }
    if (ok) ok = handle.commit();
    if (!ok) launcherConsolePrintf("App registry: remove failed label=%s\n", label);
    return ok;
}

std::vector<String> launcherAppFatLabelsForLabel(const char *label) { return loadFatLabelsForLabel(label); }

String launcherAppSpiffsLabelForLabel(const char *label) { return loadSpiffsLabelForLabel(label); }

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
bool launcherBootCurrentApp() {
    if (!bootToApp) return false;
    const esp_partition_t *bootPartition = esp_ota_get_boot_partition();
    if (!bootPartition || bootPartition->type != ESP_PARTITION_TYPE_APP ||
        bootPartition->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        return false;
    }
    std::vector<LauncherAppMetadata> apps = launcherListInstalledApps();
    if (apps.empty()) return false;
    return true;
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

static bool reportStepFailed(const String &error, const char *fallback) {
    displayError(error.length() ? error : fallback);
    return false;
}

static bool reportAppNotFound() {
    displayError("App not found");
    return false;
}

bool launcherBootAppByLabel(const char *label) {
    if (!label || !label[0]) return reportAppNotFound();

    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) {
        return reportStepFailed(error, "Partition read failed");
    }

    const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, label);
    if (!entry || !entry->isOtaApp()) return reportAppNotFound();

#if defined(FREEINK_DEVICE_PAPERMONO)
    if (strcmp(label, "payload") != 0 || !paperMonoSelectPayloadBoot(table, &error)) {
        return reportStepFailed(error, "PaperMono payload boot selection failed");
    }
#else
    if (!launcherPartitionSetOtaBoot(table, entry->subtype, &error)) {
        return reportStepFailed(error, "Boot set failed");
    }
#endif

    // Firmwares dump BLE bonds as raw structs whose layout depends on their own
    // NimBLE build, so one left behind by another app bootloops this one. Hand the
    // app back the records it wrote itself and nothing else. Failing here only costs
    // a re-pairing, so it must not hold up the boot.
    launcherBleBondsSwitchTo(label);

    lastInstalledApp = launcherAppDisplayNameForLabel(label);
    saveIntoNVS();

    return releaseHeapObjectsAndReboot();
}

bool launcherDeleteAppByLabel(const char *label) {
    if (!launcherFirmwareMutationAllowed()) {
        displayError("Firmware changes disabled");
        return false;
    }
    if (!label || !label[0]) return reportAppNotFound();

    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) {
        return reportStepFailed(error, "Partition read failed");
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
    if (appIndex < 0) return reportAppNotFound();

    String appName = launcherAppDisplayNameForLabel(label);
    std::vector<String> linkedFatLabels = launcherAppFatLabelsForLabel(label);
    String linkedSpiffsLabel = launcherAppSpiffsLabelForLabel(label);
    const bool hasLinkedSpiffs = !linkedSpiffsLabel.isEmpty() && linkedSpiffsLabel != "spiffs";

    String confirmMsg = String("Delete ") + appName;
    if (!linkedFatLabels.empty() && hasLinkedSpiffs) confirmMsg += " + FAT + SPIFFS?";
    else if (!linkedFatLabels.empty()) confirmMsg += " + FAT?";
    else if (hasLinkedSpiffs) confirmMsg += " + SPIFFS?";
    else confirmMsg += "?";

    if (!confirmAppDelete(confirmMsg)) return false;

    String appNum = loadAppNumForLabel(label);
    if (autoBackup && !appNum.isEmpty()) {
        BackupInstallInfo bkInfo = loadInstalledFromConfig(appNum);
        if (!bkInfo.partitions.empty()) {
            int choice = -1;
            std::vector<Option> opts = {
                {"Backup Data partition", [&]() { choice = 0; }},
                {"Remove Without Backup", [&]() { choice = 1; }},
                {"Cancel",                [&]() { choice = 2; }},
            };
            displayRedStripe((String("Backup data for ") + appName + "?").c_str());
            loopOptions(opts);
            if (choice == 2) return false;
            if (choice == 0) {
                if (!backupAllPartitionsForApp(appNum)) displayError("Backup failed");
            }
        }
    }

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
    if (hasLinkedSpiffs) {
        for (size_t i = 0; i < edited.entries.size(); ++i) {
            LauncherPartitionEntry &entry = edited.entries[i];
            if (entry.type == ESP_PARTITION_TYPE_DATA && (entry.subtype == 0x82 || entry.subtype == 0x83) &&
                linkedSpiffsLabel == String(entry.label)) {
                removedEntries.push_back(entry);
                edited.entries.erase(edited.entries.begin() + i);
                break;
            }
        }
    }
    normalizeOtaSubtypes(edited);
    if (!launcherPartitionCompact(edited, &error)) return reportStepFailed(error, "Compact failed");
    if (!launcherPartitionValidate(edited, &error)) return reportStepFailed(error, "Invalid table");

    displayRedStripe("Clearing boot");
    if (!launcherPartitionClearOtaBoot(table, &error)) {
        return reportStepFailed(error, "Boot clear failed");
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
            return reportStepFailed(String(), "Erase failed");
        }
    }

    displayRedStripe("Optimizing flash");
    if (!launcherPartitionMigrateMovedData(table, edited, &error)) {
        return reportStepFailed(error, "Move failed");
    }

    displayRedStripe("Writing table");
    if (!launcherPartitionWriteGeneratedTable(edited, &error)) {
        return reportStepFailed(error, "Write failed");
    }

    launcherRemoveAppMetadata(label);
    displayMsg("Restart needed");

    return releaseHeapObjectsAndReboot();
}

bool launcherRenameAppByLabel(const char *label) {
    if (!label || !label[0]) return reportAppNotFound();

    String appLabel = String(label);
    String currentName = loadAppNameForLabel(label);
    if (currentName.isEmpty()) currentName = appLabel;

    String newName = keyboard(currentName, 20, "App Name:");
    newName.trim();
    if (newName.isEmpty() || newName == String(KEY_ESCAPE) || newName == currentName) { return false; }

    if (!saveAppNameForLabel(label, newName)) return reportStepFailed(String(), "Rename failed");

    String appNum = loadAppNumForLabel(label);
    if (!appNum.isEmpty()) { updateInstalledAppName(appNum, newName); }

    displayMsg("App renamed");
    return true;
}

static void showAppBackupMenu(const String &appNum) {
    BackupInstallInfo backup = loadInstalledFromConfig(appNum);
    std::vector<Option> opts;

    for (const auto &bp : backup.partitions) {
        String status = bp.lastBackupPath.isEmpty() ? " [No backup]" : " [Backed up]";
        String optLabel = bp.type + ":" + bp.label + status;
        opts.push_back({optLabel, [appNum, bp]() {
                            displayRedStripe(("Backing up " + bp.label + "...").c_str());
                            String path = backupPartition(appNum, bp.label.c_str(), bp.type.c_str());
                            if (path.isEmpty()) {
                                displayError("Backup failed: " + bp.label);
                                return;
                            }
                            displayMsg("Backup saved!");
                        }});
    }

    opts.push_back({"Back", []() {}});
    loopOptions(opts);
}

// Restores every data partition of the app from the last backup registered in
// backupData.json. Destructive (the partition is erased first), so it asks first.
static void restoreLastDataForApp(const String &appNum) {
    int choice = -1;
    std::vector<Option> opts = {
        {"Restore", [&]() { choice = 0; }},
        {"Cancel",  [&]() { choice = 1; }},
    };
    displayRedStripe("Overwrite current data?");
    loopOptions(opts);
    if (choice != 0) return;

    if (!restoreLastBackupForApp(appNum)) displayError("Restore failed");
    else displayMsg("Data restored");
}

void launcherShowAppActions(const char *label) {
    if (!label || !label[0]) {
        displayError("App not found");
        return;
    }

    String appLabel = String(label);
    String appName = shortAppActionName(loadAppNameForLabel(label), appLabel);
    String appNum = loadAppNumForLabel(label);

    std::vector<Option> appOptions = {
        {String("Launch ") + appName, [appLabel]() { launcherBootAppByLabel(appLabel.c_str()); }  },
        {"Rename App",                [appLabel]() { launcherRenameAppByLabel(appLabel.c_str()); }},
    };

    if (!appNum.isEmpty()) {
        BackupInstallInfo backup = loadInstalledFromConfig(appNum);
        if (!backup.partitions.empty()) {
            appOptions.push_back({"Backup Data", [appNum]() { showAppBackupMenu(appNum); }});
            if (hasRestorableBackup(backup)) {
                appOptions.push_back({"Restore Last Data", [appNum]() { restoreLastDataForApp(appNum); }});
            }
        }
    }

    appOptions.push_back({String("Delete ") + appName, [appLabel]() {
                              launcherDeleteAppByLabel(appLabel.c_str());
                          }});
    appOptions.push_back({"Cancel", []() {}});

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
        displayError("No apps found");
        return;
    }
    loopOptions(appOptions);
}
