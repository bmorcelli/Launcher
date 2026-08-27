#pragma once

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_REFRESH_MANAGER)
#include <stdint.h>

class PaperMonoBsp;

enum class PaperMonoRefreshRequest : uint8_t {
    Auto,
    Full,
    Partial,
};

enum class PaperMonoRefreshExecuted : uint8_t {
    None,
    Full,
    Partial,
};

enum class PaperMonoRefreshStatus : uint8_t {
    Success,
    Busy,
    FullRequired,
    FaultLatched,
    BackendFailure,
    AllocationFailure,
    InvalidArgument,
};

struct PaperMonoRefreshResult {
    PaperMonoRefreshStatus status = PaperMonoRefreshStatus::InvalidArgument;
    PaperMonoRefreshRequest requestedType = PaperMonoRefreshRequest::Auto;
    PaperMonoRefreshExecuted executedType = PaperMonoRefreshExecuted::None;
    uint8_t partialCountAfter = 0;
    bool fullDueAfter = false;
    bool faultLatchedAfter = false;
};

// Board-local policy layer. It selects an already-proven BSP backend but owns
// neither controller transport nor the authoritative displayed-frame shadow.
class PaperMonoRefreshManager {
public:
    static constexpr uint8_t kPartialBeforeFull = 10;

    explicit PaperMonoRefreshManager(PaperMonoBsp &bsp);

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
    PaperMonoRefreshResult request(PaperMonoRefreshRequest request);
#endif

#if defined(PAPERMONO_P4_REFRESH_MANAGER)
    // Isolated validation-only partial-target selector.
    PaperMonoRefreshResult request(PaperMonoRefreshRequest request, bool inverseTarget);
#endif

    bool firstRefreshMustFull() const;
    bool refreshInProgress() const;

#if defined(PAPERMONO_P4_REFRESH_MANAGER)
    // Test-only: this target cannot establish shadow validity or bypass a backend operation.
    bool seedPartialCountForTestNine();
#endif

private:
    PaperMonoRefreshResult resultFor(
        PaperMonoRefreshRequest requested, PaperMonoRefreshStatus status, PaperMonoRefreshExecuted executed
    ) const;

    PaperMonoBsp &bsp_;
    uint8_t partialCount_ = 0;
    bool fullDue_ = false;
    bool firstRefreshMustFull_ = true;
    bool refreshInProgress_ = false;
    bool faultLatched_ = false;
    PaperMonoRefreshExecuted lastSuccessfulRefreshType_ = PaperMonoRefreshExecuted::None;
};
#endif
