#include "papermono_refresh_manager.h"

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_REFRESH_MANAGER)
#include "papermono_bsp.h"

PaperMonoRefreshManager::PaperMonoRefreshManager(PaperMonoBsp &bsp) : bsp_(bsp) {}

PaperMonoRefreshResult PaperMonoRefreshManager::resultFor(
    PaperMonoRefreshRequest requested, PaperMonoRefreshStatus status, PaperMonoRefreshExecuted executed
) const {
    PaperMonoRefreshResult result;
    result.status = status;
    result.requestedType = requested;
    result.executedType = executed;
    result.partialCountAfter = partialCount_;
    result.fullDueAfter = fullDue_;
    result.faultLatchedAfter = faultLatched_;
    return result;
}

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
PaperMonoRefreshResult PaperMonoRefreshManager::request(PaperMonoRefreshRequest request) {
    if (refreshInProgress_)
        return resultFor(request, PaperMonoRefreshStatus::Busy, PaperMonoRefreshExecuted::None);
    if (request != PaperMonoRefreshRequest::Auto && request != PaperMonoRefreshRequest::Full &&
        request != PaperMonoRefreshRequest::Partial) {
        return resultFor(request, PaperMonoRefreshStatus::InvalidArgument, PaperMonoRefreshExecuted::None);
    }
    if (!bsp_.submittedMonochromeFrameReady()) {
        return resultFor(request, PaperMonoRefreshStatus::InvalidArgument, PaperMonoRefreshExecuted::None);
    }

    PaperMonoRefreshExecuted selected = PaperMonoRefreshExecuted::None;
    if (request == PaperMonoRefreshRequest::Auto) {
        if (faultLatched_) return resultFor(request, PaperMonoRefreshStatus::FaultLatched, selected);
        selected = (firstRefreshMustFull_ || !bsp_.repeatedPartialShadowValid() || fullDue_)
                       ? PaperMonoRefreshExecuted::Full
                       : PaperMonoRefreshExecuted::Partial;
    } else if (request == PaperMonoRefreshRequest::Full) {
        selected = PaperMonoRefreshExecuted::Full;
    } else {
        if (faultLatched_) return resultFor(request, PaperMonoRefreshStatus::FaultLatched, selected);
        if (firstRefreshMustFull_ || !bsp_.repeatedPartialShadowValid() || fullDue_) {
            return resultFor(request, PaperMonoRefreshStatus::FullRequired, selected);
        }
        selected = PaperMonoRefreshExecuted::Partial;
    }

    refreshInProgress_ = true;
    PaperMonoRefreshStatus status = PaperMonoRefreshStatus::BackendFailure;
    PaperMonoRefreshExecuted executed = PaperMonoRefreshExecuted::None;
    bool success = false;
    if (selected == PaperMonoRefreshExecuted::Full) {
        executed = PaperMonoRefreshExecuted::Full;
        success = bsp_.runOtpFullPanelService().ok() && bsp_.repeatedPartialShadowValid();
    } else {
        executed = PaperMonoRefreshExecuted::Partial;
        success = bsp_.runOtpRepeatedPartialPanelService().ok() && bsp_.repeatedPartialShadowValid();
    }

    refreshInProgress_ = false;
    if (!success) {
        faultLatched_ = true;
        return resultFor(request, status, executed);
    }

    if (executed == PaperMonoRefreshExecuted::Full) {
        partialCount_ = 0;
        fullDue_ = false;
        firstRefreshMustFull_ = false;
        faultLatched_ = false;
        lastSuccessfulRefreshType_ = PaperMonoRefreshExecuted::Full;
    } else {
        ++partialCount_;
        if (partialCount_ >= kPartialBeforeFull) {
            partialCount_ = kPartialBeforeFull;
            fullDue_ = true;
        }
        lastSuccessfulRefreshType_ = PaperMonoRefreshExecuted::Partial;
    }
    return resultFor(request, PaperMonoRefreshStatus::Success, executed);
}
#endif

#if defined(PAPERMONO_P4_REFRESH_MANAGER)
PaperMonoRefreshResult PaperMonoRefreshManager::request(PaperMonoRefreshRequest request, bool inverseTarget) {
    if (refreshInProgress_)
        return resultFor(request, PaperMonoRefreshStatus::Busy, PaperMonoRefreshExecuted::None);
    if (request != PaperMonoRefreshRequest::Auto && request != PaperMonoRefreshRequest::Full &&
        request != PaperMonoRefreshRequest::Partial) {
        return resultFor(request, PaperMonoRefreshStatus::InvalidArgument, PaperMonoRefreshExecuted::None);
    }

    PaperMonoRefreshExecuted selected = PaperMonoRefreshExecuted::None;
    if (request == PaperMonoRefreshRequest::Auto) {
        if (faultLatched_) return resultFor(request, PaperMonoRefreshStatus::FaultLatched, selected);
        selected = (firstRefreshMustFull_ || !bsp_.repeatedPartialShadowValid() || fullDue_)
                       ? PaperMonoRefreshExecuted::Full
                       : PaperMonoRefreshExecuted::Partial;
    } else if (request == PaperMonoRefreshRequest::Full) {
        selected = PaperMonoRefreshExecuted::Full;
    } else {
        if (faultLatched_) return resultFor(request, PaperMonoRefreshStatus::FaultLatched, selected);
        if (firstRefreshMustFull_ || !bsp_.repeatedPartialShadowValid() || fullDue_) {
            return resultFor(request, PaperMonoRefreshStatus::FullRequired, selected);
        }
        selected = PaperMonoRefreshExecuted::Partial;
    }

    refreshInProgress_ = true;
    PaperMonoRefreshStatus status = PaperMonoRefreshStatus::BackendFailure;
    PaperMonoRefreshExecuted executed = PaperMonoRefreshExecuted::None;
    bool success = false;

    // The frozen full primitive emits its verified original quadrants. Prepare
    // that same pending target before full so its post-BUSY shadow seed matches.
    const bool backendTargetInverse = selected == PaperMonoRefreshExecuted::Full ? false : inverseTarget;
    if (!bsp_.prepareRepeatedPartialTarget(backendTargetInverse)) {
        status = PaperMonoRefreshStatus::AllocationFailure;
    } else if (selected == PaperMonoRefreshExecuted::Full) {
        executed = PaperMonoRefreshExecuted::Full;
        success = bsp_.runOtpFullPanelService().ok() && bsp_.repeatedPartialShadowValid();
    } else {
        executed = PaperMonoRefreshExecuted::Partial;
        success = bsp_.runOtpRepeatedPartialPanelService().ok() && bsp_.repeatedPartialShadowValid();
    }

    refreshInProgress_ = false;
    if (!success) {
        faultLatched_ = true;
        return resultFor(request, status, executed);
    }

    if (executed == PaperMonoRefreshExecuted::Full) {
        partialCount_ = 0;
        fullDue_ = false;
        firstRefreshMustFull_ = false;
        faultLatched_ = false;
        lastSuccessfulRefreshType_ = PaperMonoRefreshExecuted::Full;
    } else {
        ++partialCount_;
        if (partialCount_ >= kPartialBeforeFull) {
            partialCount_ = kPartialBeforeFull;
            fullDue_ = true;
        }
        lastSuccessfulRefreshType_ = PaperMonoRefreshExecuted::Partial;
    }
    return resultFor(request, PaperMonoRefreshStatus::Success, executed);
}
#endif

bool PaperMonoRefreshManager::firstRefreshMustFull() const { return firstRefreshMustFull_; }

bool PaperMonoRefreshManager::refreshInProgress() const { return refreshInProgress_; }

#if defined(PAPERMONO_P4_REFRESH_MANAGER)
bool PaperMonoRefreshManager::seedPartialCountForTestNine() {
    if (refreshInProgress_ || faultLatched_ || firstRefreshMustFull_ || !bsp_.repeatedPartialShadowValid())
        return false;
    partialCount_ = kPartialBeforeFull - 1;
    fullDue_ = false;
    return true;
}
#endif
#endif
