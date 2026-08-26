#pragma once

#include "papermono_bootstrap.h"
#include "papermono_storage.h"
#include "papermono_touch.h"

class PaperMonoBsp {
public:
    static PaperMonoBsp &instance();

    void begin();
    bool boardReady() const;
    bool beginTouch();
    bool touchReady() const;
    bool readTouch(PaperMonoTouchSample &sample);
    bool prepareStorage();
    bool cardPresent() const;
    bool storagePowered() const;
    bool storageReady() const;
    uint64_t storageCardSizeBytes() const;
    bool readStorageRoot(uint8_t maxEntries, uint8_t &entryCount) const;
    PaperMonoStorageReleaseResult releaseStorage();
    int batteryLevel() const;
    void powerOff();

private:
    bool beginAttempted_ = false;
    bool boardReady_ = false;
    PaperMonoBootstrap bootstrap_;
    PaperMonoStorage storage_;
    PaperMonoTouch touch_;
};
