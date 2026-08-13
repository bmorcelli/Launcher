#pragma once
#include <Arduino.h>
#include <vector>

struct BackupPartitionInfo {
    String type;           // "SPIFFS", "FAT", "LittleFS"
    String label;          // "spiffs", "vfs", "assets"
    String lastBackupPath; // "/bkp/a1b2c3d4-Bruce/SPIFFS.spiffs.0.bin" (empty = no backup)
};

struct BackupInstallInfo {
    String appNum;     // 8-char hash: "a1b2c3d4"
    String sdFilepath; // SD path of .bin: "/downloads/fw.bin"
    String appName;    // Display name
    std::vector<BackupPartitionInfo> partitions;
};

// Generate appNum: CRC32 of sdFilepath truncated to 8 hex chars
String generateAppNum(const String &sdFilepath);

// CRUD for config.conf "Installed" array
bool saveInstalledToConfig(const BackupInstallInfo &info);
bool updateInstalledAppName(const String &appNum, const String &newName);
bool updateInstalledBackupPath(const String &appNum, const String &label, const String &backupPath);
bool removeInstalledFromConfig(const String &appNum);
BackupInstallInfo loadInstalledFromConfig(const String &appNum);

// Search helpers
String findAppNumByFilepath(const String &sdFilepath);
String findAppNumByPartitionLabel(const String &partitionLabel);

// Backup operations
// Resolve the backup folder for an app: an existing "/bkp/{appNum}*" folder if one is
// there (matched by appNum prefix, so renames don't orphan it), otherwise the path a
// new "/bkp/{appNum}-{appName}" folder should take. Does not create the folder.
String backupDirForApp(const String &appNum);

// Returns next available index so backups don't overwrite each other
int nextBackupIndex(const String &appNum, const char *type, const char *label);

// Backup a single partition by label; saves to /bkp/{appNum}-{appName}/{type}.{label}.{x}.bin
// Returns the created file path, or empty string on error
String backupPartition(const String &appNum, const char *partitionLabel, const char *type);

// Backup all partitions registered in config.conf for this appNum
bool backupAllPartitionsForApp(const String &appNum);

// True when at least one registered partition has a backup file still present on SD
bool hasRestorableBackup(const BackupInstallInfo &info);

// Restore every partition of an app from its last backup listed in backupData.json.
// Returns false if there is nothing to restore or any partition failed.
bool restoreLastBackupForApp(const String &appNum);

// Restore a partition from a backup file on SD (uses IDF partition API)
bool restorePartitionFromBackup(const char *partitionLabel, const char *backupFilePath);

// Restore directly to a known flash offset/size (use when partition table was just written
// and the IDF runtime cache hasn't been refreshed yet)
bool restorePartitionFromBackupDirect(
    const char *partitionLabel, const char *backupFilePath, uint32_t flashOffset, uint32_t flashSize
);
