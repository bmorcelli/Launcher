#include "app_registry.h"
#include "display.h"
#include "idf/launcher_platform.h"
#include "settings.h"
#include <globals.h>
#include <memory>
#include <nvs.h>
#include <nvs_handle.hpp>
#include <nvs_flash.h>

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
    if (err != ESP_OK) {
        launcherConsolePrintf("App registry: save failed label=%s err=%d\n", label, err);
    }
    return err == ESP_OK;
}
}

std::vector<LauncherAppMetadata> launcherLoadAppRegistry() {
    std::vector<LauncherAppMetadata> apps;
    LauncherPartitionTable table;
    String error;
    if (!launcherPartitionReadCurrent(table, &error)) return apps;

    for (const LauncherPartitionEntry &entry : table.entries) {
        if (!entry.isOtaApp()) continue;
        LauncherAppMetadata app;
        app.label = String(entry.label);
        app.name = loadAppNameForLabel(entry.label);
        if (!app.name.isEmpty()) apps.push_back(app);
    }
    return apps;
}

bool launcherSaveAppMetadata(const LauncherAppMetadata &app) {
    if (app.label.isEmpty()) return false;

    bool saved = saveAppNameForLabel(app.label.c_str(), app.name);
    launcherConsolePrintf(
        "App registry: save label=%s name=%s ok=%d\n",
        app.label.c_str(),
        app.name.c_str(),
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
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err == ESP_OK) err = handle->commit();
    if (err != ESP_OK) {
        launcherConsolePrintf("App registry: remove failed label=%s err=%d\n", label, err);
    }
    return err == ESP_OK;
}

String launcherAppDisplayNameForLabel(const char *label) {
    if (!label) return "";
    String name = loadAppNameForLabel(label);
    if (!name.isEmpty()) return name;
    return String(label);
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
        if (!entry.isOtaApp()) continue;

        LauncherAppMetadata app;
        app.label = String(entry.label);
        app.name = loadAppNameForLabel(entry.label);
        if (app.name.isEmpty()) app.name = app.label;
        launcherConsolePrintf(
            "App menu item: label=%s name=%s\n",
            app.label.c_str(),
            app.name.c_str()
        );
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

    FREE_TFT
    reboot();
    return true;
}

void launcherShowAppLauncher() {
    std::vector<Option> appOptions;
    for (const LauncherAppMetadata &app : launcherListInstalledApps()) {
        String label = app.label;
        String title = app.name.isEmpty() ? app.label : app.name;
        appOptions.push_back({title, [label]() { launcherBootAppByLabel(label.c_str()); }});
    }
    appOptions.push_back({"Back", []() {}});

    if (appOptions.size() <= 1) {
        displayRedStripe("No apps found");
        launcherDelayMs(2000);
        return;
    }
    loopOptions(appOptions);
}
