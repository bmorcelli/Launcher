#pragma once

#include <WString.h>
#include <cstdint>

enum class LauncherFirmwareKind : uint8_t {
    Invalid,
    StandaloneApp,
    FullFlash,
};

struct LauncherFirmwareValidation {
    LauncherFirmwareKind kind = LauncherFirmwareKind::Invalid;
    bool esp32S3 = false;
    uint32_t fileBytes = 0;
    uint32_t imageBytes = 0;
};

// Reuses the D2-A bounded parser for install-time validation.
bool launcherValidateFirmwareFile(const String &path, LauncherFirmwareValidation &validation);

// Opens a compact, read-only report for the selected firmware file. The
// implementation uses only launcherStorageFileSize()/launcherStorageReadAt().
void launcherInspectFirmwareFile(const String &path);
