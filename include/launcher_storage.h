#pragma once

#include <WString.h>
#include <cstddef>
#include <cstdint>
#include <vector>

enum class LauncherStorageResult : uint8_t {
    NotHandled,
    Ready,
    Failed,
};

enum class LauncherStorageFileResult : uint8_t {
    NotHandled,
    Ready,
    NotFound,
    Failed,
};

struct LauncherStorageEntry {
    String name;
    String fullPath;
    bool isDirectory = false;
};

// Board backends may handle storage preparation, read-only directory enumeration,
// and the narrow config.conf text read/write seam.
// The weak defaults preserve the existing Arduino SD/SD_MMC paths for other boards.
LauncherStorageResult launcherStoragePrepare();
LauncherStorageResult
launcherStorageEnumerate(const String &folder, std::vector<LauncherStorageEntry> &entries);
LauncherStorageFileResult launcherStorageReadText(const String &path, String &contents);
LauncherStorageFileResult launcherStorageWriteText(const String &path, const String &contents);
// Bounded, read-only binary-file seam. Board backends must not mount, write, or
// otherwise mutate storage while serving these requests.
LauncherStorageFileResult launcherStorageFileSize(const String &path, uint32_t &size);
LauncherStorageFileResult
launcherStorageReadAt(const String &path, uint32_t offset, uint8_t *buffer, size_t length);
