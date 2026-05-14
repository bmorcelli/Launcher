#ifndef LAUNCHER_IDF_UPDATE_H
#define LAUNCHER_IDF_UPDATE_H

#include <Arduino.h>
#include <Stream.h>
#include <cstddef>
#include <cstdint>

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
#define LAUNCHER_UPDATE_COMMAND_SPIFFS 100

enum LauncherUpdateTarget {
    LAUNCHER_UPDATE_APP,
    LAUNCHER_UPDATE_SPIFFS,
    LAUNCHER_UPDATE_FAT_VFS,
    LAUNCHER_UPDATE_FAT_SYS,
};

using LauncherUpdateProgress = void (*)(size_t written, size_t total);

bool launcherUpdateBegin(LauncherUpdateTarget target, size_t size);
size_t launcherUpdateWrite(const uint8_t *data, size_t len);
bool launcherUpdateEnd();
void launcherUpdateAbort();
bool launcherUpdateIsFinished();
int launcherUpdateLastError();
const char *launcherUpdateLastErrorName();
bool launcherUpdateStream(
    Stream &source, size_t size, LauncherUpdateTarget target, LauncherUpdateProgress cb = nullptr
);
bool launcherUpdateTargetFromCommand(int command, LauncherUpdateTarget &target);

#endif
