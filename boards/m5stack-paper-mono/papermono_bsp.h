#pragma once

#include "papermono_bootstrap.h"
#include "papermono_frontlight.h"
#include "papermono_storage.h"
#include "papermono_touch.h"

#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH)
#include "papermono_display.h"

struct PaperMonoNoRefreshServiceResult {
    bool pwmOffPre = false;
    bool resetAsserted = false;
    bool railOn = false;
    bool spiInitialized = false;
    bool resetReleased = false;
    bool busyIdlePre = false;
    bool softwareReset = false;
    bool configured = false;
    bool pwmOffPost = false;
    bool resetSafePost = false;
    bool railOff = false;
    bool spiReleased = false;

    bool noRefreshBoundary() const {
        return pwmOffPre && resetAsserted && railOn && spiInitialized && resetReleased && busyIdlePre &&
               softwareReset && configured;
    }
    bool cleanup() const { return pwmOffPost && resetSafePost && railOff && spiReleased; }
    bool ok() const { return noRefreshBoundary() && cleanup(); }
};
#endif

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
#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH)
    PaperMonoNoRefreshServiceResult runNoRefreshPanelService();
#endif
    int batteryLevel() const;
    void powerOff();

private:
    bool setFrontlightRail(bool on);
    bool assertFrontlightEpdReset();
    void abortFrontlight(bool attemptPwmOff);
#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH)
    bool p4PwmOff();
    bool p4SetRail(bool on);
    bool p4SetReset(bool high);
#endif

    bool beginAttempted_ = false;
    bool boardReady_ = false;
    bool frontlightRailOn_ = false;
    PaperMonoBootstrap bootstrap_;
    PaperMonoFrontlight frontlight_;
    PaperMonoStorage storage_;
    PaperMonoTouch touch_;
#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH)
    PaperMonoDisplay display_;
#endif
};
