#include "backup_manager.h"
#include "display.h"
#include "idf/launcher_platform.h"
#include "launcher_capabilities.h"
#include "littlefs_patch.h"
#include "partition_table_model.h"
#include "ram_profile.h"
#include "sd_functions.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <globals.h>
#include <memory>

#define BACKUP_DATA_PATH "/bkp/backupData.json"
static constexpr size_t kBackupBufferSize = 4096;

namespace {
struct HeapCapsDeleter {
    void operator()(uint8_t *ptr) const {
        if (ptr) heap_caps_free(ptr);
    }
};

using HeapBuffer = std::unique_ptr<uint8_t, HeapCapsDeleter>;

HeapBuffer makeInternalBuffer(size_t size) {
    return HeapBuffer(static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)));
}
} // namespace

String generateAppNum(const String &sdFilepath) {
    uint32_t crc = 0xFFFFFFFF;
    for (char c : sdFilepath) {
        crc ^= (uint8_t)c;
        for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320u * (crc & 1));
    }
    crc ^= 0xFFFFFFFF;
    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", crc);
    return String(buf);
}

static bool readBackupDoc(JsonDocument &doc) {
    if (!setupSdCard()) return false;
    File f = SDM.open(BACKUP_DATA_PATH, FILE_READ);
    if (!f) return true; // file not yet created — doc stays empty/null, that's OK
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    return err == DeserializationError::Ok || err == DeserializationError::EmptyInput;
}

static bool writeBackupDoc(const JsonDocument &doc) {
    if (!setupSdCard()) return false;
    SDM.mkdir("/bkp");
    File f = SDM.open(BACKUP_DATA_PATH, FILE_WRITE);
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}

static JsonObject findInstalledEntry(JsonDocument &doc, const String &appNum) {
    JsonArray arr = doc["Installed"].as<JsonArray>();
    if (arr.isNull()) return JsonObject();
    for (JsonObject entry : arr) {
        if (entry["appNum"].as<String>() == appNum) return entry;
    }
    return JsonObject();
}

static JsonArray ensureInstalledArray(JsonDocument &doc) {
    JsonArray arr = doc["Installed"].as<JsonArray>();
    if (arr.isNull()) arr = doc["Installed"].to<JsonArray>();
    return arr;
}

bool saveInstalledToConfig(const BackupInstallInfo &info) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return false;

    JsonObject entry = findInstalledEntry(doc, info.appNum);
    if (entry.isNull()) {
        JsonArray arr = ensureInstalledArray(doc);
        if (arr.isNull()) return false;
        entry = arr.add<JsonObject>();
        if (entry.isNull()) return false;
    }

    entry["appNum"] = info.appNum;
    entry["filepath"] = info.sdFilepath;
    entry["appName"] = info.appName;

    JsonArray dataArr = entry["data"].as<JsonArray>();
    if (dataArr.isNull()) dataArr = entry["data"].to<JsonArray>();
    if (dataArr.isNull()) return false;

    for (const BackupPartitionInfo &part : info.partitions) {
        bool found = false;
        for (JsonObject dataEntry : dataArr) {
            if (dataEntry["label"].as<String>() == part.label) {
                dataEntry["type"] = part.type;
                dataEntry["filepath"] = part.lastBackupPath;
                found = true;
                break;
            }
        }
        if (!found) {
            JsonObject dataEntry = dataArr.add<JsonObject>();
            if (!dataEntry.isNull()) {
                dataEntry["type"] = part.type;
                dataEntry["label"] = part.label;
                dataEntry["filepath"] = part.lastBackupPath;
            }
        }
    }

    return writeBackupDoc(doc);
}

bool updateInstalledAppName(const String &appNum, const String &newName) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return false;

    JsonObject entry = findInstalledEntry(doc, appNum);
    if (entry.isNull()) return false;

    entry["appName"] = newName;
    return writeBackupDoc(doc);
}

bool updateInstalledBackupPath(const String &appNum, const String &label, const String &backupPath) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return false;

    JsonObject entry = findInstalledEntry(doc, appNum);
    if (entry.isNull()) return false;

    JsonArray dataArr = entry["data"].as<JsonArray>();
    if (dataArr.isNull()) return false;

    for (JsonObject dataEntry : dataArr) {
        if (dataEntry["label"].as<String>() == label) {
            dataEntry["filepath"] = backupPath;
            return writeBackupDoc(doc);
        }
    }
    return false;
}

bool removeInstalledFromConfig(const String &appNum) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return false;

    JsonArray arr = doc["Installed"].as<JsonArray>();
    if (arr.isNull()) return false;

    for (size_t i = 0; i < arr.size(); i++) {
        JsonObject entry = arr[i].as<JsonObject>();
        if (!entry.isNull() && entry["appNum"].as<String>() == appNum) {
            arr.remove(i);
            return writeBackupDoc(doc);
        }
    }
    return false;
}

BackupInstallInfo loadInstalledFromConfig(const String &appNum) {
    BackupInstallInfo result;
    JsonDocument doc;
    if (!readBackupDoc(doc)) return result;

    JsonObject entry = findInstalledEntry(doc, appNum);
    if (entry.isNull()) return result;

    result.appNum = entry["appNum"].as<String>();
    result.sdFilepath = entry["filepath"].as<String>();
    result.appName = entry["appName"].as<String>();

    JsonArray dataArr = entry["data"].as<JsonArray>();
    if (!dataArr.isNull()) {
        for (JsonObject dataEntry : dataArr) {
            BackupPartitionInfo part;
            part.type = dataEntry["type"].as<String>();
            part.label = dataEntry["label"].as<String>();
            part.lastBackupPath = dataEntry["filepath"].as<String>();
            result.partitions.push_back(part);
        }
    }
    return result;
}

String findAppNumByFilepath(const String &sdFilepath) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return "";

    JsonArray arr = doc["Installed"].as<JsonArray>();
    if (arr.isNull()) return "";

    for (JsonObject entry : arr) {
        if (entry["filepath"].as<String>() == sdFilepath) return entry["appNum"].as<String>();
    }
    return "";
}

String findAppNumByPartitionLabel(const String &partitionLabel) {
    JsonDocument doc;
    if (!readBackupDoc(doc)) return "";

    JsonArray arr = doc["Installed"].as<JsonArray>();
    if (arr.isNull()) return "";

    for (JsonObject entry : arr) {
        JsonArray dataArr = entry["data"].as<JsonArray>();
        if (dataArr.isNull()) continue;
        for (JsonObject dataEntry : dataArr) {
            if (dataEntry["label"].as<String>() == partitionLabel) return entry["appNum"].as<String>();
        }
    }
    return "";
}

// Keep folder names FAT-safe: only alphanumerics and a few separators survive.
static String sanitizeForPath(const String &name) {
    String out;
    for (size_t i = 0; i < name.length(); i++) {
        char c = name[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') out += c;
        else if (c == ' ') out += '_';
    }
    if (out.length() > 24) out = out.substring(0, 24);
    return out;
}

// Backup folders are named "/bkp/{appNum}-{appName}" so they're readable when
// restoring by hand, but they are always *found* by the appNum prefix alone —
// the app may have been renamed since the folder was created.
String backupDirForApp(const String &appNum) {
    if (appNum.isEmpty()) return "";
    if (!setupSdCard()) return "/bkp/" + appNum;

    File root = SDM.open("/bkp");
    if (root && root.isDirectory()) {
        for (File f = root.openNextFile(); f; f = root.openNextFile()) {
            String name = String(f.name());
            bool isDir = f.isDirectory();
            f.close();
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (isDir && name.startsWith(appNum)) {
                root.close();
                return "/bkp/" + name;
            }
        }
    }
    if (root) root.close();

    String suffix = sanitizeForPath(loadInstalledFromConfig(appNum).appName);
    if (suffix.isEmpty()) return "/bkp/" + appNum;
    return "/bkp/" + appNum + "-" + suffix;
}

static int nextBackupIndexInDir(const String &dir, const char *type, const char *label) {
    int idx = 0;
    File root = SDM.open(dir);
    if (!root || !root.isDirectory()) return 0;

    while (true) {
        char name[64];
        snprintf(name, sizeof(name), "%s.%s.%d.bin", type, label, idx);
        String fullPath = dir + "/" + name;
        if (!SDM.exists(fullPath)) break;
        idx++;
    }
    root.close();
    return idx;
}

int nextBackupIndex(const String &appNum, const char *type, const char *label) {
    return nextBackupIndexInDir(backupDirForApp(appNum), type, label);
}

// Resolve a data partition's flash offset/size by label. Prefers the IDF
// runtime cache, but falls back to the partition table read straight from
// flash. That fallback matters right after an install: the new table is
// already on flash but the IDF cache still reflects the pre-install layout,
// so esp_partition_find_first() can't see a just-created partition yet.
static bool resolveDataRegion(const char *label, uint32_t &flashOffset, uint32_t &regionSize) {
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (part) {
        flashOffset = part->address;
        regionSize = part->size;
        return true;
    }

    LauncherPartitionTable table;
    if (!launcherPartitionReadCurrentUnchecked(table)) return false;
    const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, label);
    if (!entry) return false;
    flashOffset = entry->offset;
    regionSize = entry->size;
    return true;
}

static String backupFail(const char *label) {
    displayError("Backup failed: " + String(label));
    return "";
}

// Same idea for the restore paths below.
static bool restoreFail(const char *label) {
    displayError("Restore failed: " + String(label));
    return false;
}

String backupPartition(const String &appNum, const char *partitionLabel, const char *type) {
    RAM_LOG("backupPartition-start");
    if (!setupSdCard()) return "";

    String dir = backupDirForApp(appNum);
    if (dir.isEmpty()) return backupFail(partitionLabel);
    int idx = nextBackupIndexInDir(dir, type, partitionLabel);
    char fname[64];
    snprintf(fname, sizeof(fname), "%s.%s.%d.bin", type, partitionLabel, idx);
    String outPath = dir + "/" + fname;

    SDM.mkdir("/bkp");
    SDM.mkdir(dir);

    uint32_t flashOffset = 0;
    uint32_t regionSize = 0;
    if (!resolveDataRegion(partitionLabel, flashOffset, regionSize)) return backupFail(partitionLabel);

    File outFile = SDM.open(outPath, FILE_WRITE);
    if (!outFile) return backupFail(partitionLabel);

    HeapBuffer buf = makeInternalBuffer(kBackupBufferSize);
    if (!buf) {
        outFile.close();
        SDM.remove(outPath);
        displayError("Backup failed: no RAM");
        return "";
    }
    size_t written = 0;
    size_t total = regionSize;

    prog_handler = 1;
    progressHandler(0, total);
    displayRedStripe((String("Backing up ") + partitionLabel).c_str());

    while (written < total) {
        size_t chunk = min(kBackupBufferSize, total - written);
        esp_err_t err = esp_flash_read(nullptr, buf.get(), flashOffset + written, chunk);
        if (err != ESP_OK) {
            outFile.close();
            SDM.remove(outPath);
            return backupFail(partitionLabel);
        }
        if (outFile.write(buf.get(), chunk) != chunk) {
            outFile.close();
            SDM.remove(outPath);
            return backupFail(partitionLabel);
        }
        written += chunk;
        progressHandler(written, total);
    }
    outFile.close();

    updateInstalledBackupPath(appNum, String(partitionLabel), outPath);
    displayMsg("Backup saved");
    RAM_LOG("backupPartition-end");
    return outPath;
}

bool backupAllPartitionsForApp(const String &appNum) {
    BackupInstallInfo info = loadInstalledFromConfig(appNum);
    if (info.appNum.isEmpty()) return false;

    bool ok = true;
    for (const BackupPartitionInfo &part : info.partitions) {
        String path = backupPartition(appNum, part.label.c_str(), part.type.c_str());
        if (path.isEmpty()) ok = false;
    }
    return ok;
}

bool hasRestorableBackup(const BackupInstallInfo &info) {
    if (info.partitions.empty()) return false;
    if (!setupSdCard()) return false;
    for (const BackupPartitionInfo &part : info.partitions) {
        if (!part.lastBackupPath.isEmpty() && SDM.exists(part.lastBackupPath)) return true;
    }
    return false;
}

bool restoreLastBackupForApp(const String &appNum) {
    if (!launcherFirmwareMutationAllowed()) return false;
    BackupInstallInfo info = loadInstalledFromConfig(appNum);
    if (!hasRestorableBackup(info)) return false;

    bool ok = true;
    for (const BackupPartitionInfo &part : info.partitions) {
        if (part.lastBackupPath.isEmpty() || !SDM.exists(part.lastBackupPath)) continue;
        if (!restorePartitionFromBackup(part.label.c_str(), part.lastBackupPath.c_str())) ok = false;
    }
    return ok;
}

bool restorePartitionFromBackup(const char *partitionLabel, const char *backupFilePath) {
    if (!launcherFirmwareMutationAllowed()) return false;
    RAM_LOG("restorePartition-start");
    if (!setupSdCard()) return false;

    File inFile = SDM.open(backupFilePath, FILE_READ);
    if (!inFile) return restoreFail(partitionLabel);

    size_t fileSize = inFile.size();
    if (fileSize == 0) {
        inFile.close();
        return restoreFail(partitionLabel);
    }

    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partitionLabel);
    if (!part) {
        inFile.close();
        log_w("restorePartitionFromBackup: partition '%s' not found on device, skipping", partitionLabel);
        return true;
    }

    const bool truncated = fileSize > part->size;
    if (truncated) fileSize = part->size;

    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        inFile.close();
        return restoreFail(partitionLabel);
    }

    prog_handler = 1;
    progressHandler(0, fileSize);
    displayRedStripe((String("Restoring ") + partitionLabel).c_str());

    HeapBuffer buf = makeInternalBuffer(kBackupBufferSize);
    if (!buf) {
        inFile.close();
        displayError("Restore failed: no RAM");
        return false;
    }
    size_t written = 0;
    while (written < fileSize) {
        size_t chunk = min(kBackupBufferSize, fileSize - written);
        if (inFile.read(buf.get(), chunk) != (int)chunk) {
            inFile.close();
            return restoreFail(partitionLabel);
        }
        err = esp_partition_write(part, written, buf.get(), chunk);
        if (err != ESP_OK) {
            inFile.close();
            return restoreFail(partitionLabel);
        }
        written += chunk;
        progressHandler(written, fileSize);
    }
    inFile.close();

    if (truncated) {
        String patchError;
        if (!launcherPatchReducedLittlefsSuperblocks(part->address, part->size, &patchError)) {
            launcherConsolePrintf("Restore patch failed label=%s: %s\n", partitionLabel, patchError.c_str());
        }
        displayMsg("Backup > Partition, data can be lost");
    } else {
        displayMsg("Data restored");
    }
    RAM_LOG("restorePartition-end");
    return true;
}

bool restorePartitionFromBackupDirect(
    const char *partitionLabel, const char *backupFilePath, uint32_t flashOffset, uint32_t flashSize
) {
    if (!launcherFirmwareMutationAllowed()) return false;
    RAM_LOG("restorePartitionDirect-start");
    if (!setupSdCard()) return false;

    File inFile = SDM.open(backupFilePath, FILE_READ);
    if (!inFile) return restoreFail(partitionLabel);

    size_t fileSize = inFile.size();
    if (fileSize == 0) {
        inFile.close();
        return restoreFail(partitionLabel);
    }

    const bool truncated = fileSize > flashSize;
    if (truncated) fileSize = flashSize;

    HeapBuffer buf = makeInternalBuffer(kBackupBufferSize);
    if (!buf) {
        inFile.close();
        displayError("Restore failed: no RAM");
        return false;
    }
    size_t written = 0;

    prog_handler = 1;
    progressHandler(0, fileSize);
    displayRedStripe((String("Restoring ") + partitionLabel).c_str());

    while (written < fileSize) {
        size_t chunk = min(kBackupBufferSize, fileSize - written);
        uint32_t addr = flashOffset + written;

        if ((written % kBackupBufferSize) == 0) {
            esp_err_t err = esp_flash_erase_region(nullptr, addr, kBackupBufferSize);
            if (err != ESP_OK) {
                inFile.close();
                return restoreFail(partitionLabel);
            }
        }

        if (inFile.read(buf.get(), chunk) != (int)chunk) {
            inFile.close();
            return restoreFail(partitionLabel);
        }

        esp_err_t err = esp_flash_write(nullptr, buf.get(), addr, chunk);
        if (err != ESP_OK) {
            inFile.close();
            return restoreFail(partitionLabel);
        }

        written += chunk;
        progressHandler(written, fileSize);
    }
    inFile.close();

    if (truncated) {
        String patchError;
        if (!launcherPatchReducedLittlefsSuperblocks(flashOffset, flashSize, &patchError)) {
            launcherConsolePrintf("Restore patch failed label=%s: %s\n", partitionLabel, patchError.c_str());
        }
        displayMsg("Backup > Partition, data can be lost");
    } else {
        displayMsg("Data restored");
    }
    RAM_LOG("restorePartitionDirect-end");
    return true;
}
