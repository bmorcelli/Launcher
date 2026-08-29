#ifndef LAUNCHER_IDF_UPDATE_H
#define LAUNCHER_IDF_UPDATE_H

#include <Stream.h>
#include <WString.h>
#include <cstddef>
#include <cstdint>
#include <esp_partition.h>

#define LAUNCHER_UPDATE_ERROR_OK 0
#define LAUNCHER_UPDATE_ERROR_WRITE 1
#define LAUNCHER_UPDATE_ERROR_ERASE 2
#define LAUNCHER_UPDATE_ERROR_READ 3
#define LAUNCHER_UPDATE_ERROR_SPACE 4
#define LAUNCHER_UPDATE_ERROR_SIZE 5
#define LAUNCHER_UPDATE_ERROR_STREAM 6
#define LAUNCHER_UPDATE_ERROR_MAGIC_BYTE 8
#define LAUNCHER_UPDATE_ERROR_ACTIVATE 9
#define LAUNCHER_UPDATE_ERROR_NO_PARTITION 10
#define LAUNCHER_UPDATE_ERROR_BAD_ARGUMENT 11
#define LAUNCHER_UPDATE_ERROR_ABORT 12

#define LAUNCHER_UPDATE_COMMAND_FLASH 0

enum LauncherUpdateTarget {
    LAUNCHER_UPDATE_APP,
    LAUNCHER_UPDATE_DATA,
};

using LauncherUpdateProgress = void (*)(size_t written, size_t total);

size_t launcherUpdateWrite(const uint8_t *data, size_t len);
void launcherUpdateAbort();
bool launcherUpdateIsFinished();
int launcherUpdateLastError();
const char *launcherUpdateLastErrorName();

bool launcherRawUpdateBegin(uint32_t address, size_t partitionSize, size_t imageSize, bool appImage);
size_t launcherRawUpdateWrite(const uint8_t *data, size_t len);
bool launcherRawUpdateEnd();
bool launcherRawErase(uint32_t address, size_t size);
bool launcherRawPrepareDataPartition(uint32_t address, size_t size);
bool launcherClearCoredump();
bool launcherUpdateErasePartition(const esp_partition_t *partition);
bool launcherUpdateCopyPartition(
    const esp_partition_t *source, const esp_partition_t *destination, LauncherUpdateProgress cb = nullptr
);
bool launcherUpdateRepairPartitionTable(uint32_t removeOtaAddress, bool *removedOta = nullptr);
bool launcherRawUpdateStream(
    Stream &source, uint32_t address, size_t partitionSize, size_t imageSize, bool appImage,
    LauncherUpdateProgress cb = nullptr
);

// Fixed app-slot writer. This deliberately uses only the firmware-install
// capability; raw/runtime mutation capabilities are not prerequisites.
bool launcherInstallAppToPartition(
    const String &sourcePath, const esp_partition_t *partition, size_t imageSize,
    LauncherUpdateProgress cb = nullptr
);

#endif
