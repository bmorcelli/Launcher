#include "littlefs_patch.h"

#include "idf/launcher_platform.h"

#include <Arduino.h>
#include <algorithm>
#include <esp_flash.h>
#include <memory>

namespace {
constexpr uint32_t kLittlefsSuperblockType = 0x0FF;
constexpr uint32_t kLittlefsCcrcTypeMask = 0x780;
constexpr uint32_t kLittlefsCcrcType = 0x500;
constexpr size_t kLittlefsHeaderSize = 0x2C;
constexpr size_t kLittlefsMagicOffset = 0x08;
constexpr size_t kLittlefsInlineStructOffset = 0x10;
constexpr size_t kLittlefsBlockSizeOffset = 0x18;
constexpr size_t kLittlefsBlockCountOffset = 0x1C;
constexpr size_t kProbeSize = 0x40;

void setError(String *error, const char *message) {
    if (error) *error = message;
}

uint32_t readLe32(const uint8_t *data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

uint32_t readBe32(const uint8_t *data) {
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

void writeLe32(uint8_t *data, uint32_t value) {
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    data[2] = (value >> 16) & 0xFF;
    data[3] = (value >> 24) & 0xFF;
}

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t littlefsCrc(uint32_t crc, const void *buffer, size_t size) {
    static const uint32_t table[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
        0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };

    const uint8_t *data = static_cast<const uint8_t *>(buffer);
    for (size_t i = 0; i < size; ++i) {
        crc = (crc >> 4) ^ table[(crc ^ (data[i] >> 0)) & 0x0F];
        crc = (crc >> 4) ^ table[(crc ^ (data[i] >> 4)) & 0x0F];
    }
    return crc;
}

bool looksLikeLittlefsSuperblock(const uint8_t *data, size_t size) {
    if (!data || size < kProbeSize) return false;
    if (memcmp(data + kLittlefsMagicOffset, "littlefs", 8) != 0) return false;
    const uint32_t blockSize = readLe32(data + kLittlefsBlockSizeOffset);
    const uint32_t blockCount = readLe32(data + kLittlefsBlockCountOffset);
    if (blockSize == 0 || blockCount == 0) return false;
    if ((blockSize % LAUNCHER_FLASH_SECTOR_SIZE) != 0) return false;
    return true;
}

bool patchLittlefsRootCommit(uint8_t *block, uint32_t blockSize, uint32_t newBlockCount, bool &patched) {
    patched = false;
    if (!block || blockSize < kLittlefsHeaderSize) return false;
    if (memcmp(block + kLittlefsMagicOffset, "littlefs", 8) != 0) return false;

    const uint32_t storedBlockSize = readLe32(block + kLittlefsBlockSizeOffset);
    if (storedBlockSize != blockSize) return false;

    uint32_t crc = littlefsCrc(0xFFFFFFFF, block, sizeof(uint32_t));
    uint32_t previousTag = 0xFFFFFFFF;
    size_t offset = sizeof(uint32_t);
    bool sawSuperblock = false;

    while (offset + sizeof(uint32_t) <= blockSize) {
        const uint32_t rawTag = readBe32(block + offset);
        const uint32_t decodedTag = rawTag ^ previousTag;
        if ((decodedTag & 0x80000000U) != 0) return false;

        const uint32_t tag = decodedTag & 0x7FFFFFFFU;
        const uint32_t type = (tag >> 20) & 0x7FFU;
        uint32_t dataSize = tag & 0x3FFU;
        if (dataSize == 0x3FFU) dataSize = 0;

        const size_t tagSize = sizeof(uint32_t) + dataSize;
        if (offset + tagSize > blockSize) return false;

        if (offset == sizeof(uint32_t) && type == kLittlefsSuperblockType &&
            memcmp(block + offset + sizeof(uint32_t), "littlefs", 8) == 0) {
            sawSuperblock = true;
        }

        if ((type & kLittlefsCcrcTypeMask) == kLittlefsCcrcType) {
            crc = littlefsCrc(crc, block + offset, sizeof(uint32_t));
            if (offset + 2 * sizeof(uint32_t) > blockSize) return false;
            if (readLe32(block + offset + sizeof(uint32_t)) != crc) return false;
            if (!sawSuperblock) return false;

            const uint32_t oldBlockCount = readLe32(block + kLittlefsBlockCountOffset);
            if (oldBlockCount <= newBlockCount) return true;

            writeLe32(block + kLittlefsBlockCountOffset, newBlockCount);

            uint32_t newCrc = littlefsCrc(0xFFFFFFFF, block, sizeof(uint32_t));
            uint32_t currentPreviousTag = 0xFFFFFFFF;
            size_t currentOffset = sizeof(uint32_t);
            while (currentOffset + sizeof(uint32_t) <= blockSize) {
                const uint32_t currentRawTag = readBe32(block + currentOffset);
                const uint32_t currentDecodedTag = currentRawTag ^ currentPreviousTag;
                if ((currentDecodedTag & 0x80000000U) != 0) return false;

                const uint32_t currentTag = currentDecodedTag & 0x7FFFFFFFU;
                const uint32_t currentType = (currentTag >> 20) & 0x7FFU;
                uint32_t currentDataSize = currentTag & 0x3FFU;
                if (currentDataSize == 0x3FFU) currentDataSize = 0;

                const size_t currentTagSize = sizeof(uint32_t) + currentDataSize;
                if (currentOffset + currentTagSize > blockSize) return false;

                if ((currentType & kLittlefsCcrcTypeMask) == kLittlefsCcrcType) {
                    newCrc = littlefsCrc(newCrc, block + currentOffset, sizeof(uint32_t));
                    writeLe32(block + currentOffset + sizeof(uint32_t), newCrc);
                    patched = true;
                    return true;
                }

                newCrc = littlefsCrc(newCrc, block + currentOffset, currentTagSize);
                currentPreviousTag = currentTag;
                currentOffset += currentTagSize;
            }
            return false;
        }

        crc = littlefsCrc(crc, block + offset, tagSize);
        previousTag = tag;
        offset += tagSize;
    }

    return false;
}
} // namespace

bool launcherPartitionLooksLikeLittlefs(uint32_t address, uint32_t partitionSize) {
    if (address == 0 || partitionSize < kProbeSize) return false;
    uint8_t probe[kProbeSize];
    if (esp_flash_read(nullptr, probe, address, sizeof(probe)) != ESP_OK) return false;
    return looksLikeLittlefsSuperblock(probe, sizeof(probe));
}

bool launcherPatchReducedLittlefsSuperblocks(
    uint32_t address, uint32_t partitionSize, String *error, bool *patched
) {
    if (patched) *patched = false;
    if (address == 0 || partitionSize < kProbeSize) return true;

    uint8_t probe[kProbeSize];
    if (esp_flash_read(nullptr, probe, address, sizeof(probe)) != ESP_OK) {
        setError(error, "Could not read partition header");
        return false;
    }

    if (!looksLikeLittlefsSuperblock(probe, sizeof(probe))) return true;

    const uint32_t blockSize = readLe32(probe + kLittlefsBlockSizeOffset);
    const uint32_t blockCount = readLe32(probe + kLittlefsBlockCountOffset);
    if (blockSize == 0 || blockCount == 0) return true;
    if (partitionSize % blockSize != 0) return true;

    const uint32_t newBlockCount = partitionSize / blockSize;
    if (newBlockCount == 0 || newBlockCount >= blockCount) return true;

    const uint32_t rewriteSize = alignUp(std::min(partitionSize, blockSize * 2), LAUNCHER_FLASH_SECTOR_SIZE);
    std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[rewriteSize]);
    if (!buffer) {
        setError(error, "Could not allocate patch buffer");
        return false;
    }

    if (esp_flash_read(nullptr, buffer.get(), address, rewriteSize) != ESP_OK) {
        setError(error, "Could not read littlefs blocks");
        return false;
    }

    bool modified = false;
    const uint32_t blocksToCheck = std::min<uint32_t>(2, partitionSize / blockSize);
    for (uint32_t blockIndex = 0; blockIndex < blocksToCheck; ++blockIndex) {
        bool blockPatched = false;
        uint8_t *block = buffer.get() + blockIndex * blockSize;
        if (patchLittlefsRootCommit(block, blockSize, newBlockCount, blockPatched)) {
            modified = modified || blockPatched;
        }
    }

    if (!modified) return true;

    if (esp_flash_erase_region(nullptr, address, rewriteSize) != ESP_OK) {
        setError(error, "Could not erase patched littlefs sectors");
        return false;
    }
    if (esp_flash_write(nullptr, buffer.get(), address, rewriteSize) != ESP_OK) {
        setError(error, "Could not write patched littlefs sectors");
        return false;
    }

    if (patched) *patched = true;
    launcherConsolePrintf(
        "Patched littlefs superblock at 0x%08X: block_size=0x%08X old_count=0x%08X new_count=0x%08X\n",
        address,
        blockSize,
        blockCount,
        newBlockCount
    );
    return true;
}
