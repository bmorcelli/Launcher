#pragma once

#include <cstdint>
#include <sdmmc_cmd.h>

#include "launcher_storage.h"

struct PaperMonoStorageReleaseResult {
    bool unmounted = true;
    bool powerOff = true;

    bool ok() const { return unmounted && powerOff; }
};

class PaperMonoStorage {
public:
    bool prepare();
    bool cardPresent() const;
    bool powered() const;
    bool ready() const;
    uint64_t cardSizeBytes() const;
    bool readRoot(uint8_t maxEntries, uint8_t &entryCount) const;
    bool enumerate(const String &folder, std::vector<LauncherStorageEntry> &entries) const;
    PaperMonoStorageReleaseResult release();

private:
    bool cardPresent_ = false;
    bool ready_ = false;
    bool powerEnabled_ = false;
    bool cleanupFailed_ = false;
    sdmmc_card_t *card_ = nullptr;
};
