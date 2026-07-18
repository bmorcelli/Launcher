#include "partition_fs.h"
#include "display.h"
#include "littlefs_patch.h"
#include "mykeyboard.h"
#include "utils.h"
#include <FFat.h>
#include <FS.h>
#include <LittleFS.h>
#include <algorithm>
#include <esp_flash.h>
#include <globals.h>

namespace {

struct PfsEntry {
    String name;
    String path;
    bool isDir = false;
    size_t size = 0;
};

bool looksLikeFat(uint32_t address, uint32_t size) {
    if (address == 0 || size < 512) return false;
    uint8_t sector[512];
    if (esp_flash_read(nullptr, sector, address, sizeof(sector)) != ESP_OK) return false;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return false;
    return sector[0] == 0xEB || sector[0] == 0xE9;
}

bool confirmPfsAction(const String &message) {
    bool confirmed = false;
    std::vector<Option> confirmOptions = {
        {"Confirm", [&]() { confirmed = true; } },
        {"Cancel",  [&]() { confirmed = false; }},
    };
    displayRedStripe(message);
    loopOptions(confirmOptions);
    return confirmed;
}

std::vector<PfsEntry> listPfsDirectory(fs::FS &fs, const String &folder) {
    std::vector<PfsEntry> entries;
    File root = fs.open(folder);
    if (!root || !root.isDirectory()) return entries;

    File child = root.openNextFile();
    while (child) {
        PfsEntry entry;
        entry.name = String(child.name());
        entry.path = String(child.path());
        entry.isDir = child.isDirectory();
        entry.size = entry.isDir ? 0 : child.size();
        entries.push_back(entry);
        child.close();
        child = root.openNextFile();
    }
    root.close();

    std::sort(entries.begin(), entries.end(), [](const PfsEntry &a, const PfsEntry &b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        String an = a.name;
        an.toUpperCase();
        String bn = b.name;
        bn.toUpperCase();
        return an < bn;
    });
    return entries;
}

bool deletePfsPath(fs::FS &fs, const String &path, bool isDir) {
    if (!isDir) return fs.remove(path);

    bool success = true;
    for (const PfsEntry &child : listPfsDirectory(fs, path)) {
        success &= deletePfsPath(fs, child.path, child.isDir);
    }
    success &= fs.rmdir(path);
    return success;
}

bool renamePfsPath(fs::FS &fs, const String &path, const String &currentName) {
    String newName = keyboard(currentName, 76, "Type the new Name:");
    if (newName.isEmpty() || newName == String(KEY_ESCAPE) || newName == currentName) return false;

    String parent = path.substring(0, path.lastIndexOf('/'));
    if (parent.isEmpty()) parent = "/";
    String newPath = (parent == "/" ? "" : parent) + "/" + newName;
    return fs.rename(path, newPath);
}

void showPfsEntryDetails(const PfsEntry &entry) {
    tft->fillScreen(BGCOLOR);
    resetTftDisplay(8, 8, FGCOLOR, FP, BGCOLOR, BGCOLOR);
    tftprintln(entry.isDir ? "Folder" : "File", 8);
    tftprintln(String("Name: ") + entry.name, 8);
    tftprintln(String("Path: ") + entry.path, 8);
    if (!entry.isDir) tftprintln(String("Size: ") + launcherHumanSize(entry.size), 8);
    tft->setTextColor(ALCOLOR);
    tftprintln("Press Select", 8);
#if defined(HAS_TOUCH)
    TouchFooter();
#endif
    while (!check(SelPress)) yield();
    while (check(SelPress)) yield();
}

void browseMountedFs(fs::FS &fs, const String &label, const String &fsTypeName) {
    returnToMenu = false;
    String folder = "/";

    while (!returnToMenu) {
        std::vector<PfsEntry> entries = listPfsDirectory(fs, folder);
        std::vector<Option> menuOptions;
        menuOptions.push_back({String("* ") + fsTypeName + ": " + label, []() { yield(); }});

        if (folder != "/") {
            menuOptions.push_back(
                {"..",
                 [&folder]() {
                     String parent = folder.substring(0, folder.lastIndexOf('/'));
                     folder = parent.isEmpty() ? "/" : parent;
                 },
                 ALCOLOR}
            );
        }

        for (const PfsEntry &entry : entries) {
            String row = entry.isDir ? ("/" + entry.name) : (entry.name + "  " + launcherHumanSize(entry.size));
            menuOptions.push_back(
                {row,
                 [&fs, &folder, entry]() {
                     int selected = 100;
                     std::vector<Option> entryOptions = {};
                     entryOptions.push_back({"Details", [&]() { selected = 0; }});
                     if (entry.isDir) entryOptions.push_back({"Open", [&]() { selected = 1; }});
                     entryOptions.push_back({"Rename", [&]() { selected = 2; }});
                     entryOptions.push_back({"Delete", [&]() { selected = 3; }});
                     entryOptions.push_back({"Back", [&]() { selected = 4; }});
                     loopOptions(entryOptions);

                     if (selected == 0) showPfsEntryDetails(entry);
                     else if (selected == 1) folder = entry.path;
                     else if (selected == 2) {
                         if (!renamePfsPath(fs, entry.path, entry.name)) displayError("Rename failed");
                     } else if (selected == 3) {
                         String prompt = entry.isDir ? "Delete folder and contents?" : "Delete file?";
                         if (confirmPfsAction(prompt) && !deletePfsPath(fs, entry.path, entry.isDir)) {
                             displayError("Delete failed");
                         }
                     }
                 },
                 entry.isDir ? uint16_t(FGCOLOR - 0x1111) : FGCOLOR}
            );
        }

        menuOptions.push_back({"Back", [&]() { returnToMenu = true; }, ALCOLOR});
        loopOptions(menuOptions, false, ALCOLOR, BGCOLOR, false, 0);
        tft->fillScreen(BGCOLOR);
    }
}

} // namespace

void launcherBrowsePartitionFiles(const LauncherPartitionEntry &entry) {
    if (!entry.isData()) {
        displayError("Not a data partition");
        return;
    }

    displayRedStripe("Detecting filesystem");
    const bool isLittleFs = launcherPartitionLooksLikeLittlefs(entry.offset, entry.size);
    const bool isFat = !isLittleFs && looksLikeFat(entry.offset, entry.size);

    if (!isLittleFs && !isFat) {
        displayError("Unknown or unformatted filesystem");
        return;
    }

    FFat.end();
    LittleFS.end();

    if (isLittleFs) {
        displayRedStripe("Mounting LittleFS");
        if (!LittleFS.begin(false, "/plfs", 10, entry.label)) {
            displayError("LittleFS mount failed");
            return;
        }
        browseMountedFs(LittleFS, entry.label, "LittleFS");
        LittleFS.end();
    } else {
        displayRedStripe("Mounting FAT");
        if (!FFat.begin(false, "/pfat", 5, entry.label)) {
            displayError("FAT mount failed");
            return;
        }
        browseMountedFs(FFat, entry.label, "FAT");
        FFat.end();
    }
}
