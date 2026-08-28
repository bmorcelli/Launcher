#include "papermono_storage.h"

#include <Arduino.h>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <driver/sdmmc_host.h>
#include <esp_err.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <sys/stat.h>

#include "papermono_bsp.h"
#include "papermono_sys_i2c_lock.h"
#include "vendor/freeink_board/M5Ioe1.h"

namespace {

constexpr char kMountPoint[] = "/sdcard";
constexpr uint32_t kPowerSettleMs = 300;

constexpr gpio_num_t kData3Pin = GPIO_NUM_8;
constexpr gpio_num_t kData2Pin = GPIO_NUM_9;
constexpr gpio_num_t kData1Pin = GPIO_NUM_10;
constexpr gpio_num_t kData0Pin = GPIO_NUM_11;
constexpr gpio_num_t kCmdPin = GPIO_NUM_12;
constexpr gpio_num_t kClkPin = GPIO_NUM_13;

bool handlesConfigPath(const String &path) { return path == "/config.conf"; }

String configHostPath(const String &path) { return String(kMountPoint) + path; }

String storageHostPath(const String &path) {
    if (!path.startsWith("/")) return String();
    return String(kMountPoint) + path;
}

} // namespace

bool PaperMonoStorage::prepare() {
    if (ready_) return true;
    if (cleanupFailed_) return false;
    if (powerEnabled_ || card_ != nullptr) {
        if (!release().ok()) return false;
    }

    bool detectHigh = true;
    {
        PaperMonoSysI2cGuard guard;
        if (!guard.locked() || !freeink::m5ioe1::read(freeink::m5ioe1::PIN_TF_DETECT, &detectHigh))
            return false;
    }
    cardPresent_ = !detectHigh;
    if (!cardPresent_) return false;

    {
        PaperMonoSysI2cGuard guard;
        if (!guard.locked() || !freeink::m5ioe1::write(freeink::m5ioe1::PIN_SD_POWER, true)) return false;
    }
    powerEnabled_ = true;
    delay(kPowerSettleMs);

    esp_vfs_fat_sdmmc_mount_config_t mountConfig = {};
    mountConfig.format_if_mount_failed = false;
    mountConfig.max_files = 8;
    mountConfig.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = kClkPin;
    slot.cmd = kCmdPin;
    slot.d0 = kData0Pin;
    slot.d1 = kData1Pin;
    slot.d2 = kData2Pin;
    slot.d3 = kData3Pin;

    if (esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot, &mountConfig, &card_) != ESP_OK) {
        card_ = nullptr;
        ready_ = false;
        release();
        return false;
    }
    ready_ = true;
    return true;
}

bool PaperMonoStorage::cardPresent() const { return cardPresent_; }

bool PaperMonoStorage::powered() const { return powerEnabled_; }

bool PaperMonoStorage::ready() const { return ready_; }

uint64_t PaperMonoStorage::cardSizeBytes() const {
    return card_ ? static_cast<uint64_t>(card_->csd.capacity) * card_->csd.sector_size : 0;
}

bool PaperMonoStorage::readRoot(uint8_t maxEntries, uint8_t &entryCount) const {
    entryCount = 0;
    if (!ready_) return false;

    DIR *root = opendir(kMountPoint);
    if (root == nullptr) return false;
    while (entryCount < maxEntries && readdir(root) != nullptr) ++entryCount;
    closedir(root);
    return true;
}

bool PaperMonoStorage::enumerate(const String &folder, std::vector<LauncherStorageEntry> &entries) const {
    entries.clear();
    if (!ready_) return false;

    String normalized = folder;
    if (!normalized.startsWith("/")) normalized = "/" + normalized;
    if (!normalized.endsWith("/")) normalized += "/";
    const String hostPath = String(kMountPoint) + normalized;

    DIR *root = opendir(hostPath.c_str());
    if (root == nullptr) return false;
    while (struct dirent *entry = readdir(root)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        bool isDirectory = entry->d_type == DT_DIR;
        bool isRegular = entry->d_type == DT_REG;
        if (!isDirectory && !isRegular && entry->d_type == DT_UNKNOWN) {
            struct stat info = {};
            const String entryPath = hostPath + entry->d_name;
            if (stat(entryPath.c_str(), &info) != 0) continue;
            isDirectory = S_ISDIR(info.st_mode);
            isRegular = S_ISREG(info.st_mode);
        }
        if (!isDirectory && !isRegular) continue;

        LauncherStorageEntry item;
        item.name = entry->d_name;
        item.fullPath = normalized + entry->d_name;
        item.isDirectory = isDirectory;
        entries.push_back(item);
    }
    closedir(root);
    return true;
}

PaperMonoStorageReleaseResult PaperMonoStorage::release() {
    PaperMonoStorageReleaseResult result;
    if (card_ != nullptr) {
        result.unmounted = esp_vfs_fat_sdcard_unmount(kMountPoint, card_) == ESP_OK;
        card_ = nullptr;
    }
    ready_ = false;
    if (powerEnabled_) {
        PaperMonoSysI2cGuard guard;
        result.powerOff = guard.locked() && freeink::m5ioe1::write(freeink::m5ioe1::PIN_SD_POWER, false);
        if (result.powerOff) powerEnabled_ = false;
    }
    if (!result.unmounted) cleanupFailed_ = true;
    return result;
}

LauncherStorageResult launcherStoragePrepare() {
    return PaperMonoBsp::instance().prepareStorage() ? LauncherStorageResult::Ready
                                                     : LauncherStorageResult::Failed;
}

LauncherStorageResult
launcherStorageEnumerate(const String &folder, std::vector<LauncherStorageEntry> &entries) {
    PaperMonoBsp &bsp = PaperMonoBsp::instance();
    if (!bsp.storageReady()) {
        entries.clear();
        return LauncherStorageResult::Failed;
    }
    return bsp.enumerateStorage(folder, entries) ? LauncherStorageResult::Ready
                                                 : LauncherStorageResult::Failed;
}

LauncherStorageFileResult launcherStorageReadText(const String &path, String &contents) {
    if (!handlesConfigPath(path)) return LauncherStorageFileResult::NotHandled;

    FILE *file = fopen(configHostPath(path).c_str(), "rb");
    if (file == nullptr)
        return errno == ENOENT ? LauncherStorageFileResult::NotFound : LauncherStorageFileResult::Failed;

    String loaded;
    char buffer[256];
    size_t count = 0;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) != 0) loaded.concat(buffer, count);
    const bool readOk = ferror(file) == 0;
    const bool closed = fclose(file) == 0;
    const bool ok = readOk && closed;
    if (!ok) return LauncherStorageFileResult::Failed;

    contents = loaded;
    return LauncherStorageFileResult::Ready;
}

LauncherStorageFileResult launcherStorageWriteText(const String &path, const String &contents) {
    if (!handlesConfigPath(path)) return LauncherStorageFileResult::NotHandled;

    FILE *file = fopen(configHostPath(path).c_str(), "wb");
    if (file == nullptr) return LauncherStorageFileResult::Failed;

    const size_t expected = contents.length();
    const size_t written = fwrite(contents.c_str(), 1, expected, file);
    const bool flushed = fflush(file) == 0;
    const bool closed = fclose(file) == 0;
    return written == expected && flushed && closed ? LauncherStorageFileResult::Ready
                                                    : LauncherStorageFileResult::Failed;
}

LauncherStorageFileResult launcherStorageFileSize(const String &path, uint32_t &size) {
    if (!PaperMonoBsp::instance().storageReady()) return LauncherStorageFileResult::Failed;

    const String hostPath = storageHostPath(path);
    if (hostPath.isEmpty()) return LauncherStorageFileResult::NotHandled;
    FILE *file = fopen(hostPath.c_str(), "rb");
    if (file == nullptr)
        return errno == ENOENT ? LauncherStorageFileResult::NotFound : LauncherStorageFileResult::Failed;

    const bool seekOk = fseek(file, 0, SEEK_END) == 0;
    const long end = seekOk ? ftell(file) : -1;
    const bool closed = fclose(file) == 0;
    if (!seekOk || end < 0 || static_cast<uint64_t>(end) > UINT32_MAX || !closed)
        return LauncherStorageFileResult::Failed;

    size = static_cast<uint32_t>(end);
    return LauncherStorageFileResult::Ready;
}

LauncherStorageFileResult
launcherStorageReadAt(const String &path, uint32_t offset, uint8_t *buffer, size_t length) {
    if (!PaperMonoBsp::instance().storageReady() || (buffer == nullptr && length != 0))
        return LauncherStorageFileResult::Failed;

    const String hostPath = storageHostPath(path);
    if (hostPath.isEmpty()) return LauncherStorageFileResult::NotHandled;
    FILE *file = fopen(hostPath.c_str(), "rb");
    if (file == nullptr)
        return errno == ENOENT ? LauncherStorageFileResult::NotFound : LauncherStorageFileResult::Failed;

    const bool seekOk = fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
    const size_t received = seekOk ? fread(buffer, 1, length, file) : 0;
    const bool readOk = received == length && ferror(file) == 0;
    const bool closed = fclose(file) == 0;
    return readOk && closed ? LauncherStorageFileResult::Ready : LauncherStorageFileResult::Failed;
}
