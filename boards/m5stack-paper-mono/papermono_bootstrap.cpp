#include "papermono_bootstrap.h"

#if defined(PAPERMONO_P2_NO_REFRESH_BOOTSTRAP)
#include "vendor/freeink_board/PaperMonoBoard.h"

namespace {

constexpr uint8_t kBootstrapAttempts = 3;
constexpr uint32_t kBootstrapRetryDelayMs = 150;

enum class BootstrapAttemptResult : uint8_t {
    success,
    pm1Failure,
    ioe1Failure,
};

BootstrapAttemptResult runFreeInkBootstrapAttempt() {
    // Keep the established FreeInk board-service order while making the
    // official UserDemo's bounded attempt envelope Bootstrap-owned.
    freeink::m5pm1::beginBus();
    const bool pm1Ready = freeink::m5pm1::clearWakeSource() && freeink::m5pm1::configureAppPowerButton() &&
                          freeink::m5pm1::updateReg(freeink::m5pm1::REG_PWR_CFG, freeink::m5pm1::LED_R_EN, 0);
    if (!pm1Ready) return BootstrapAttemptResult::pm1Failure;

    return freeink::m5ioe1::configureOutputs() ? BootstrapAttemptResult::success
                                               : BootstrapAttemptResult::ioe1Failure;
}

} // namespace
#endif

bool PaperMonoBootstrap::begin() {
    if (beginAttempted_) return ready_;
    beginAttempted_ = true;

#if defined(PAPERMONO_P2_NO_REFRESH_BOOTSTRAP)
    for (uint8_t attempt = 0; attempt < kBootstrapAttempts; ++attempt) {
        const auto result = runFreeInkBootstrapAttempt();
        if (result == BootstrapAttemptResult::success) {
            ready_ = true;
            break;
        }

        if (result == BootstrapAttemptResult::ioe1Failure) {
            // The vendored IOE1 begin() memoizes a failed 0x6F -> 0x4F
            // resolution as 0xFF. Keep that compatibility detail private to
            // this retry adapter so the next official-style attempt can use
            // the existing resolver again.
            freeink::m5ioe1::g_addr = 0;
        }
        delay(kBootstrapRetryDelayMs);
    }
#endif

    return ready_;
}

bool PaperMonoBootstrap::ready() const { return ready_; }
