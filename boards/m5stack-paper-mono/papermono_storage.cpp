#include "papermono_storage.h"

#include <Arduino.h>
#include <dirent.h>
#include <driver/sdmmc_host.h>
#include <esp_err.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

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

} // namespace

bool PaperMonoStorage::prepare() {
    if (ready_) return true;
    if (cleanupFailed_) return false;
    if (powerEnabled_ || card_ != nullptr) {
        if (!release().ok()) return false;
    }

    bool detectHigh = true;
    if (!freeink::m5ioe1::read(freeink::m5ioe1::PIN_TF_DETECT, &detectHigh)) return false;
    cardPresent_ = !detectHigh;
    if (!cardPresent_) return false;

    if (!freeink::m5ioe1::write(freeink::m5ioe1::PIN_SD_POWER, true)) return false;
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

PaperMonoStorageReleaseResult PaperMonoStorage::release() {
    PaperMonoStorageReleaseResult result;
    if (card_ != nullptr) {
        result.unmounted = esp_vfs_fat_sdcard_unmount(kMountPoint, card_) == ESP_OK;
        card_ = nullptr;
    }
    ready_ = false;
    if (powerEnabled_) {
        result.powerOff = freeink::m5ioe1::write(freeink::m5ioe1::PIN_SD_POWER, false);
        if (result.powerOff) powerEnabled_ = false;
    }
    if (!result.unmounted) cleanupFailed_ = true;
    return result;
}
