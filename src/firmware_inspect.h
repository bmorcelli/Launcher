#pragma once

#include <WString.h>

// Opens a compact, read-only report for the selected firmware file. The
// implementation uses only launcherStorageFileSize()/launcherStorageReadAt().
void launcherInspectFirmwareFile(const String &path);
