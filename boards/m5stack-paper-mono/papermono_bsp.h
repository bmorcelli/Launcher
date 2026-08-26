#pragma once

#include "papermono_bootstrap.h"
#include "papermono_frontlight.h"
#include "papermono_storage.h"
#include "papermono_touch.h"

#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH) || defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) ||                  \
    defined(PAPERMONO_P4_OTP_FULL_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||                      \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
#include "papermono_display.h"
#endif

#if defined(PAPERMONO_P4_OTP_FULL_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||                      \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
struct PaperMonoOtpFullRefreshResult {
    bool pwmOffPre = false;
    bool resetAsserted = false;
    bool railOn = false;
    bool spiInitialized = false;
    bool resetReleased = false;
    bool busyIdlePre = false;
    bool configured = false;
    bool frameWritten = false;
    bool stage1Control = false;
    bool stage1Activated = false;
    bool stage1BusyDone = false;
    bool stage2Control = false;
    bool stage2Activated = false;
    bool stage2BusyDone = false;
    uint8_t activationCount = 0;
    bool pwmOffPost = false;
    bool resetSafePost = false;
    bool railOff = false;
    bool spiReleased = false;

    bool cleanup() const { return pwmOffPost && resetSafePost && railOff && spiReleased; }
    bool ok() const {
        return pwmOffPre && resetAsserted && railOn && spiInitialized && resetReleased && busyIdlePre &&
               configured && frameWritten && stage1Control && stage1Activated && stage1BusyDone &&
               stage2Control && stage2Activated && stage2BusyDone && activationCount == 2 && cleanup();
    }
};
#endif

#if defined(PAPERMONO_P4_REPEATED_PARTIAL) || defined(PAPERMONO_P4_REFRESH_MANAGER)
struct PaperMonoRepeatedPartialResult {
    bool stateValid = false;
    bool pwmOffPre = false;
    bool resetAsserted = false;
    bool railOn = false;
    bool spiInitialized = false;
    bool resetReleased = false;
    bool busyIdlePre = false;
    bool configured = false;
    bool planesStaged = false;
    bool updateControl = false;
    uint8_t activationCount = 0;
    bool busyDone = false;
    bool shadowCommitted = false;
    bool pwmOffPost = false;
    bool resetSafePost = false;
    bool railOff = false;
    bool spiReleased = false;

    bool cleanup() const { return pwmOffPost && resetSafePost && railOff && spiReleased; }
    bool ok() const {
        return stateValid && pwmOffPre && resetAsserted && railOn && spiInitialized && resetReleased &&
               busyIdlePre && configured && planesStaged && updateControl && activationCount == 1 &&
               busyDone && shadowCommitted && cleanup();
    }
};
#endif

#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH)
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

#if defined(PAPERMONO_P4_OTP_SINGLE_REFRESH)
struct PaperMonoOtpSingleRefreshResult {
    bool pwmOffPre = false;
    bool resetAsserted = false;
    bool railOn = false;
    bool spiInitialized = false;
    bool resetReleased = false;
    bool busyIdlePre = false;
    bool configured = false;
    bool whiteBaselineWritten = false;
    bool frameWritten = false;
    bool updateControl = false;
    uint8_t activationCount = 0;
    bool busyDone = false;
    bool pwmOffPost = false;
    bool resetSafePost = false;
    bool railOff = false;
    bool spiReleased = false;

    bool cleanup() const { return pwmOffPost && resetSafePost && railOff && spiReleased; }
    bool ok() const {
        return pwmOffPre && resetAsserted && railOn && spiInitialized && resetReleased && busyIdlePre &&
               configured && whiteBaselineWritten && frameWritten && updateControl && activationCount == 1 &&
               busyDone && cleanup();
    }
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
#if defined(PAPERMONO_P4_OTP_SINGLE_REFRESH)
    PaperMonoOtpSingleRefreshResult runOtpSinglePanelService();
#endif
#if defined(PAPERMONO_P4_OTP_FULL_REFRESH)
    PaperMonoOtpFullRefreshResult runOtpFullPanelService();
#endif
#if defined(PAPERMONO_P4_REPEATED_PARTIAL) || defined(PAPERMONO_P4_REFRESH_MANAGER)
    bool prepareRepeatedPartialTarget(bool inverse);
    bool repeatedPartialShadowValid() const;
    PaperMonoOtpFullRefreshResult runOtpFullPanelService();
    PaperMonoRepeatedPartialResult runOtpRepeatedPartialPanelService();
#endif
    int batteryLevel() const;
    void powerOff();

private:
    bool setFrontlightRail(bool on);
    bool assertFrontlightEpdReset();
    void abortFrontlight(bool attemptPwmOff);
#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH) || defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) ||                  \
    defined(PAPERMONO_P4_OTP_FULL_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||                      \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
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
#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH) || defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) ||                  \
    defined(PAPERMONO_P4_OTP_FULL_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||                      \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
    PaperMonoDisplay display_;
#endif
};
