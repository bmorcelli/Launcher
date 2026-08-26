#pragma once

#include "papermono_bootstrap.h"
#include "papermono_frontlight.h"
#include "papermono_storage.h"
#include "papermono_touch.h"

struct PaperMonoFrontlightReleaseResult {
    bool pwmOff = false;
    bool railOff = false;
    bool epdResetAsserted = false;
    bool pwmReleased = false;

    bool ok() const { return pwmOff && railOff && epdResetAsserted && pwmReleased; }
};

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
    bool prepareFrontlight();
    bool setFrontlight(uint8_t percent);
    bool frontlightOff();
    PaperMonoFrontlightReleaseResult releaseFrontlight();
    bool frontlightPwmOff() const;
    bool frontlightRailOn() const;
    bool frontlightEpdResetAsserted() const;
    int batteryLevel() const;
    void powerOff();

private:
    bool setFrontlightRail(bool on);
    bool assertFrontlightEpdReset();
    void abortFrontlight(bool attemptPwmOff);

    bool beginAttempted_ = false;
    bool boardReady_ = false;
    bool frontlightRailOn_ = false;
    PaperMonoBootstrap bootstrap_;
    PaperMonoFrontlight frontlight_;
    PaperMonoStorage storage_;
    PaperMonoTouch touch_;
};
