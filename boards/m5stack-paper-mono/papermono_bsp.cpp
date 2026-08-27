#include "papermono_bsp.h"

#if !defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
#include <M5Unified.h>
#endif

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P3_FRONTLIGHT_BRINGUP) ||             \
    defined(PAPERMONO_P4_DISPLAY_NO_REFRESH) || defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) ||                  \
    defined(PAPERMONO_P4_OTP_FULL_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||                      \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
#include "vendor/freeink_board/M5Ioe1.h"
#endif

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_DISPLAY_NO_REFRESH) ||             \
    defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) || defined(PAPERMONO_P4_OTP_FULL_REFRESH) ||                    \
    defined(PAPERMONO_P4_REPEATED_PARTIAL) || defined(PAPERMONO_P4_REFRESH_MANAGER)
#include "vendor/freeink_board/M5Pm1.h"
#endif

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_REFRESH_MANAGER)
#include "papermono_refresh_manager.h"
#endif

namespace {

#if defined(PAPERMONO_P2_NO_REFRESH_BOOTSTRAP) && !defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)

constexpr uint32_t kTelemetryAttachWindowMs = 10000;

void beginP2Telemetry() {
    // CrossPoint's known-good PaperMono cold-start timing for USB Serial/JTAG.
    delay(250);
    Serial.begin(115200);
    Serial.setTxTimeoutMs(1);
}

void waitForP2TelemetryHost() {
    // The device may be powered off when the host monitor starts. Wait only for
    // a bounded USB CDC attach window, then emit the single P29 report anyway.
    const uint32_t attachStartMs = millis();
    while (!Serial && millis() - attachStartMs < kTelemetryAttachWindowMs) { delay(10); }
}

void emitP2Telemetry(bool boardServiceReady) {
    // FreeInk's production ensureBooted() intentionally exposes one aggregate
    // result. Do not add PM1/IOE probes solely for telemetry.
    Serial.println("P29_BEGIN");
    Serial.printf("P29_BOARD_SERVICE=%d\n", boardServiceReady);
    Serial.println("P29_DONE");
}

void stopP2SafeRuntime() {
    // P2-only test isolation: do not return to Launcher setup(), which would
    // otherwise initialize Dummy TFT, SD, input, and the normal UI/runtime.
    for (;;) { delay(1000); }
}

#if defined(PAPERMONO_P3_TOUCH_BRINGUP)
void runP3TouchTelemetry(PaperMonoBsp &bsp) {
    Serial.printf("P3_TOUCH_INIT=%d\n", bsp.touchReady());
    PaperMonoTouchSample last;
    bool haveLast = false;
    for (;;) {
        PaperMonoTouchSample sample;
        if (bsp.readTouch(sample) &&
            (!haveLast || sample.touched != last.touched ||
             (sample.touched && (abs(sample.x - last.x) > 2 || abs(sample.y - last.y) > 2)))) {
            if (sample.touched) Serial.printf("P3_TOUCH=1 X=%d Y=%d\n", sample.x, sample.y);
            else Serial.println("P3_TOUCH=0");
            last = sample;
            haveLast = true;
        }
        delay(50);
    }
}
#endif

#if defined(PAPERMONO_P3_SD_BRINGUP)
void runP3SdTelemetry(PaperMonoBsp &bsp) {
    const bool mounted = bsp.prepareStorage();
    Serial.printf("P3_SD_DET=%d\n", bsp.cardPresent());
    Serial.printf("P3_SD_POWER=%d\n", bsp.storagePowered());
    Serial.printf("P3_SD_MOUNT=%d\n", mounted);
    if (mounted) {
        Serial.printf(
            "P3_SD_SIZE_MB=%llu\n", static_cast<unsigned long long>(bsp.storageCardSizeBytes() / 1024 / 1024)
        );
        uint8_t rootEntries = 0;
        const bool rootRead = bsp.readStorageRoot(8, rootEntries);
        Serial.printf("P3_SD_ROOT_READ=%d ENTRIES=%u\n", rootRead, rootEntries);
    }
    const PaperMonoStorageReleaseResult cleanup = bsp.releaseStorage();
    Serial.printf("P3_SD_UNMOUNT=%d\n", cleanup.unmounted);
    Serial.printf("P3_SD_POWER_OFF=%d\n", cleanup.powerOff);
    for (;;) { delay(1000); }
}
#endif

#if defined(PAPERMONO_P3_FRONTLIGHT_BRINGUP)
constexpr uint8_t kP3FrontlightTestPercent = 20;
constexpr uint32_t kP3FrontlightObservationWindowMs = 1000;

void runP3FrontlightTelemetry(PaperMonoBsp &bsp) {
    const bool prepared = bsp.prepareFrontlight();
    Serial.printf("P3_FL_PWM_OFF_PRE=%d\n", bsp.frontlightPwmOff());
    Serial.printf("P3_FL_EPD_RST=%d\n", bsp.frontlightEpdResetAsserted());
    Serial.printf("P3_FL_RAIL_ON=%d\n", bsp.frontlightRailOn());

    const bool levelSet = prepared && bsp.setFrontlight(kP3FrontlightTestPercent);
    Serial.printf("P3_FL_LEVEL_SET=%d\n", levelSet);
    Serial.printf("P3_FL_LEVEL_PERCENT=%u\n", static_cast<unsigned>(kP3FrontlightTestPercent));
    if (levelSet) {
        // Bounded human-observation interval; not an electrical settle delay.
        delay(kP3FrontlightObservationWindowMs);
    }

    const PaperMonoFrontlightReleaseResult cleanup = bsp.releaseFrontlight();
    Serial.printf("P3_FL_PWM_OFF_POST=%d\n", cleanup.pwmOff);
    Serial.printf("P3_FL_RAIL_OFF=%d\n", cleanup.railOff);
    Serial.printf("P3_FL_RST_SAFE_POST=%d\n", cleanup.epdResetAsserted);
    Serial.printf("P3_FL_CLEANUP=%d\n", cleanup.ok());
    for (;;) { delay(1000); }
}
#endif

#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH)
void runP4NoRefreshTelemetry(PaperMonoBsp &bsp) {
    Serial.println("P4_DNR_BEGIN");
    const PaperMonoNoRefreshServiceResult result = bsp.runNoRefreshPanelService();
    Serial.printf("P4_DNR_PWM_OFF_PRE=%d\n", result.pwmOffPre);
    Serial.printf("P4_DNR_RST_ASSERTED=%d\n", result.resetAsserted);
    Serial.printf("P4_DNR_RAIL_ON=%d\n", result.railOn);
    Serial.printf("P4_DNR_SPI_INIT=%d\n", result.spiInitialized);
    Serial.printf("P4_DNR_RST_RELEASED=%d\n", result.resetReleased);
    Serial.printf("P4_DNR_BUSY_IDLE_PRE=%d\n", result.busyIdlePre);
    Serial.printf("P4_DNR_SW_RESET=%d\n", result.softwareReset);
    Serial.printf("P4_DNR_CONFIG=%d\n", result.configured);
    Serial.printf("P4_DNR_NO_REFRESH_BOUNDARY=%d\n", result.noRefreshBoundary());
    Serial.printf("P4_DNR_PWM_OFF_POST=%d\n", result.pwmOffPost);
    Serial.printf("P4_DNR_RST_SAFE_POST=%d\n", result.resetSafePost);
    Serial.printf("P4_DNR_RAIL_OFF=%d\n", result.railOff);
    Serial.printf("P4_DNR_SPI_RELEASE=%d\n", result.spiReleased);
    Serial.printf("P4_DNR_CLEANUP=%d\n", result.cleanup());
    Serial.println("P4_DNR_DONE");
    for (;;) { delay(1000); }
}
#endif

#if defined(PAPERMONO_P4_OTP_SINGLE_REFRESH)
void runP4OtpTelemetry(PaperMonoBsp &bsp) {
    Serial.println("P4_OTP_BEGIN");
    const PaperMonoOtpSingleRefreshResult result = bsp.runOtpSinglePanelService();
    Serial.printf("P4_OTP_PWM_OFF_PRE=%d\n", result.pwmOffPre);
    Serial.printf("P4_OTP_RST_ASSERTED=%d\n", result.resetAsserted);
    Serial.printf("P4_OTP_RAIL_ON=%d\n", result.railOn);
    Serial.printf("P4_OTP_SPI_INIT=%d\n", result.spiInitialized);
    Serial.printf("P4_OTP_RST_RELEASED=%d\n", result.resetReleased);
    Serial.printf("P4_OTP_BUSY_IDLE_PRE=%d\n", result.busyIdlePre);
    Serial.printf("P4_OTP_CONFIG=%d\n", result.configured);
    Serial.printf("P4_OTP_WHITE_BASELINE_WRITTEN=%d\n", result.whiteBaselineWritten);
    Serial.printf("P4_OTP_FRAME_WRITTEN=%d\n", result.frameWritten);
    Serial.printf("P4_OTP_UPDATE_CTRL=%d\n", result.updateControl);
    Serial.printf("P4_OTP_ACTIVATION_COUNT=%u\n", static_cast<unsigned>(result.activationCount));
    Serial.printf("P4_OTP_BUSY_DONE=%d\n", result.busyDone);
    Serial.printf("P4_OTP_PWM_OFF_POST=%d\n", result.pwmOffPost);
    Serial.printf("P4_OTP_RST_SAFE_POST=%d\n", result.resetSafePost);
    Serial.printf("P4_OTP_RAIL_OFF=%d\n", result.railOff);
    Serial.printf("P4_OTP_SPI_RELEASE=%d\n", result.spiReleased);
    Serial.printf("P4_OTP_CLEANUP=%d\n", result.cleanup());
    Serial.println("P4_OTP_DONE");
    for (;;) { delay(1000); }
}
#endif

#if defined(PAPERMONO_P4_OTP_FULL_REFRESH)
void runP4FullTelemetry(PaperMonoBsp &bsp) {
    Serial.println("P4_FULL_BEGIN");
    const PaperMonoOtpFullRefreshResult result = bsp.runOtpFullPanelService();
    Serial.printf("P4_FULL_PWM_OFF_PRE=%d\n", result.pwmOffPre);
    Serial.printf("P4_FULL_RST_ASSERTED=%d\n", result.resetAsserted);
    Serial.printf("P4_FULL_RAIL_ON=%d\n", result.railOn);
    Serial.printf("P4_FULL_SPI_INIT=%d\n", result.spiInitialized);
    Serial.printf("P4_FULL_RST_RELEASED=%d\n", result.resetReleased);
    Serial.printf("P4_FULL_BUSY_IDLE_PRE=%d\n", result.busyIdlePre);
    Serial.printf("P4_FULL_CONFIG=%d\n", result.configured);
    Serial.printf("P4_FULL_FRAME_WRITTEN=%d\n", result.frameWritten);
    Serial.printf("P4_FULL_STAGE1_CTRL=%d\n", result.stage1Control);
    Serial.printf("P4_FULL_STAGE1_ACTIVATED=%d\n", result.stage1Activated);
    Serial.printf("P4_FULL_STAGE1_BUSY_DONE=%d\n", result.stage1BusyDone);
    Serial.printf("P4_FULL_STAGE2_CTRL=%d\n", result.stage2Control);
    Serial.printf("P4_FULL_STAGE2_ACTIVATED=%d\n", result.stage2Activated);
    Serial.printf("P4_FULL_STAGE2_BUSY_DONE=%d\n", result.stage2BusyDone);
    Serial.printf("P4_FULL_ACTIVATION_COUNT=%u\n", static_cast<unsigned>(result.activationCount));
    Serial.printf("P4_FULL_PWM_OFF_POST=%d\n", result.pwmOffPost);
    Serial.printf("P4_FULL_RST_SAFE_POST=%d\n", result.resetSafePost);
    Serial.printf("P4_FULL_RAIL_OFF=%d\n", result.railOff);
    Serial.printf("P4_FULL_SPI_RELEASE=%d\n", result.spiReleased);
    Serial.printf("P4_FULL_CLEANUP=%d\n", result.cleanup());
    Serial.println("P4_FULL_DONE");
    for (;;) { delay(1000); }
}
#endif

#if defined(PAPERMONO_P4_REPEATED_PARTIAL)
void runP4RepeatedPartialTelemetry(PaperMonoBsp &bsp) {
    Serial.println("P4_RP_BEGIN");

    const bool baseTargetReady = bsp.prepareRepeatedPartialTarget(false);
    const PaperMonoOtpFullRefreshResult base =
        baseTargetReady ? bsp.runOtpFullPanelService() : PaperMonoOtpFullRefreshResult{};
    Serial.printf("P4_RP_BASE_FULL_OK=%d\n", base.ok());
    Serial.printf("P4_RP_BASE_STAGE1_ACTIVATED=%d\n", base.stage1Activated);
    Serial.printf("P4_RP_BASE_STAGE1_BUSY_DONE=%d\n", base.stage1BusyDone);
    Serial.printf("P4_RP_BASE_STAGE2_ACTIVATED=%d\n", base.stage2Activated);
    Serial.printf("P4_RP_BASE_STAGE2_BUSY_DONE=%d\n", base.stage2BusyDone);
    Serial.printf("P4_RP_BASE_ACTIVATION_COUNT=%u\n", static_cast<unsigned>(base.activationCount));
    Serial.printf("P4_RP_BASE_VALID=%d\n", bsp.repeatedPartialShadowValid());
    if (!base.ok() || !bsp.repeatedPartialShadowValid()) {
        Serial.println("P4_RP_FAIL=BASE");
        for (;;) { delay(1000); }
    }

    Serial.println("P4_RP_A_BEGIN");
    const bool aTargetReady = bsp.prepareRepeatedPartialTarget(true);
    const PaperMonoRepeatedPartialResult a =
        aTargetReady ? bsp.runOtpRepeatedPartialPanelService() : PaperMonoRepeatedPartialResult{};
    Serial.printf("P4_RP_A_PLANES_READY=%d\n", a.planesStaged);
    Serial.printf("P4_RP_A_ACTIVATION_COUNT=%u\n", static_cast<unsigned>(a.activationCount));
    Serial.printf("P4_RP_A_BUSY_DONE=%d\n", a.busyDone);
    Serial.printf("P4_RP_A_SHADOW_COMMIT=%d\n", a.shadowCommitted);
    Serial.printf("P4_RP_A_CLEANUP=%d\n", a.cleanup());
    if (!a.ok()) {
        Serial.println("P4_RP_FAIL=A");
        for (;;) { delay(1000); }
    }

    Serial.println("P4_RP_B_BEGIN");
    const bool bTargetReady = bsp.prepareRepeatedPartialTarget(false);
    const PaperMonoRepeatedPartialResult b =
        bTargetReady ? bsp.runOtpRepeatedPartialPanelService() : PaperMonoRepeatedPartialResult{};
    Serial.printf("P4_RP_B_PLANES_READY=%d\n", b.planesStaged);
    Serial.printf("P4_RP_B_ACTIVATION_COUNT=%u\n", static_cast<unsigned>(b.activationCount));
    Serial.printf("P4_RP_B_BUSY_DONE=%d\n", b.busyDone);
    Serial.printf("P4_RP_B_SHADOW_COMMIT=%d\n", b.shadowCommitted);
    Serial.printf("P4_RP_B_CLEANUP=%d\n", b.cleanup());
    if (!b.ok()) {
        Serial.println("P4_RP_FAIL=B");
        for (;;) { delay(1000); }
    }

    Serial.printf("P4_RP_FINAL_SHADOW_VALID=%d\n", bsp.repeatedPartialShadowValid());
    Serial.println("P4_RP_DONE");
    for (;;) { delay(1000); }
}
#endif

#if defined(PAPERMONO_P4_REFRESH_MANAGER)
void runP4RefreshManagerTelemetry(PaperMonoBsp &bsp) {
    PaperMonoRefreshManager manager(bsp);
    Serial.println("P4_RM_BEGIN");
    Serial.printf("P4_RM_COLD_FIRST_FULL_REQUIRED=%d\n", manager.firstRefreshMustFull());

    const PaperMonoRefreshResult auto0 = manager.request(PaperMonoRefreshRequest::Auto, false);
    Serial.printf("P4_RM_AUTO0_EXECUTED_FULL=%d\n", auto0.executedType == PaperMonoRefreshExecuted::Full);
    Serial.printf("P4_RM_AUTO0_OK=%d\n", auto0.status == PaperMonoRefreshStatus::Success);
    Serial.printf("P4_RM_COUNT=%u\n", static_cast<unsigned>(auto0.partialCountAfter));
    Serial.printf("P4_RM_FULL_DUE=%d\n", auto0.fullDueAfter);
    Serial.printf("P4_RM_FAULT=%d\n", auto0.faultLatchedAfter);
    if (auto0.status != PaperMonoRefreshStatus::Success) {
        Serial.println("P4_RM_FAIL=AUTO0");
        for (;;) { delay(1000); }
    }

    const PaperMonoRefreshResult partial1 = manager.request(PaperMonoRefreshRequest::Auto, true);
    Serial.printf("P4_RM_PARTIAL1_EXECUTED=%d\n", partial1.executedType == PaperMonoRefreshExecuted::Partial);
    Serial.printf("P4_RM_PARTIAL1_OK=%d\n", partial1.status == PaperMonoRefreshStatus::Success);
    if (partial1.status != PaperMonoRefreshStatus::Success) {
        Serial.println("P4_RM_FAIL=PARTIAL1");
        for (;;) { delay(1000); }
    }

    const bool seeded = manager.seedPartialCountForTestNine();
    Serial.printf("P4_RM_TEST_COUNT_SEEDED=%u\n", seeded ? 9U : 0U);
    if (!seeded) {
        Serial.println("P4_RM_FAIL=SEED");
        for (;;) { delay(1000); }
    }

    const PaperMonoRefreshResult threshold = manager.request(PaperMonoRefreshRequest::Partial, false);
    Serial.printf("P4_RM_THRESHOLD_PARTIAL_OK=%d\n", threshold.status == PaperMonoRefreshStatus::Success);
    Serial.printf("P4_RM_COUNT_AFTER_THRESHOLD=%u\n", static_cast<unsigned>(threshold.partialCountAfter));
    Serial.printf("P4_RM_FULL_DUE_AFTER_THRESHOLD=%d\n", threshold.fullDueAfter);
    if (threshold.status != PaperMonoRefreshStatus::Success) {
        Serial.println("P4_RM_FAIL=THRESHOLD");
        for (;;) { delay(1000); }
    }

    const PaperMonoRefreshResult dueAuto = manager.request(PaperMonoRefreshRequest::Auto, false);
    Serial.printf(
        "P4_RM_AUTO_FULL_DUE_EXECUTED_FULL=%d\n", dueAuto.executedType == PaperMonoRefreshExecuted::Full
    );
    Serial.printf("P4_RM_RECOVERY_FULL_OK=%d\n", dueAuto.status == PaperMonoRefreshStatus::Success);
    Serial.printf("P4_RM_COUNT_FINAL=%u\n", static_cast<unsigned>(dueAuto.partialCountAfter));
    Serial.printf("P4_RM_FULL_DUE_FINAL=%d\n", dueAuto.fullDueAfter);
    Serial.printf("P4_RM_FAULT_FINAL=%d\n", dueAuto.faultLatchedAfter);
    Serial.println("P4_RM_DONE");
    for (;;) { delay(1000); }
}
#endif

#endif

} // namespace

PaperMonoBsp &PaperMonoBsp::instance() {
    static PaperMonoBsp bsp;
    return bsp;
}

void PaperMonoBsp::begin() {
    if (beginAttempted_) return;
    beginAttempted_ = true;

    delay(500);

#if defined(PAPERMONO_P2_NO_REFRESH_BOOTSTRAP) && !defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
    beginP2Telemetry();
    boardReady_ = bootstrap_.begin();
#if defined(PAPERMONO_P3_TOUCH_BRINGUP)
    beginTouch();
#endif
    waitForP2TelemetryHost();
    emitP2Telemetry(boardReady_);
#if defined(PAPERMONO_P3_TOUCH_BRINGUP)
    runP3TouchTelemetry(*this);
#elif defined(PAPERMONO_P3_SD_BRINGUP)
    runP3SdTelemetry(*this);
#elif defined(PAPERMONO_P3_FRONTLIGHT_BRINGUP)
    runP3FrontlightTelemetry(*this);
#elif defined(PAPERMONO_P4_DISPLAY_NO_REFRESH)
    runP4NoRefreshTelemetry(*this);
#elif defined(PAPERMONO_P4_OTP_SINGLE_REFRESH)
    runP4OtpTelemetry(*this);
#elif defined(PAPERMONO_P4_OTP_FULL_REFRESH)
    runP4FullTelemetry(*this);
#elif defined(PAPERMONO_P4_REPEATED_PARTIAL)
    runP4RepeatedPartialTelemetry(*this);
#elif defined(PAPERMONO_P4_REFRESH_MANAGER)
    runP4RefreshManagerTelemetry(*this);
#else
    stopP2SafeRuntime();
#endif
#else
    boardReady_ = bootstrap_.begin();
    if (boardReady_) beginTouch();
#endif
}

bool PaperMonoBsp::boardReady() const { return boardReady_; }

bool PaperMonoBsp::beginTouch() {
    if (!boardReady_) return false;
    return touch_.begin();
}

bool PaperMonoBsp::touchReady() const { return touch_.ready(); }

bool PaperMonoBsp::readTouch(PaperMonoTouchSample &sample) { return touch_.read(sample); }

bool PaperMonoBsp::prepareStorage() {
    if (!boardReady_) return false;
    return storage_.prepare();
}

bool PaperMonoBsp::cardPresent() const { return storage_.cardPresent(); }

bool PaperMonoBsp::storagePowered() const { return storage_.powered(); }

bool PaperMonoBsp::storageReady() const { return storage_.ready(); }

uint64_t PaperMonoBsp::storageCardSizeBytes() const { return storage_.cardSizeBytes(); }

bool PaperMonoBsp::readStorageRoot(uint8_t maxEntries, uint8_t &entryCount) const {
    return storage_.readRoot(maxEntries, entryCount);
}

PaperMonoStorageReleaseResult PaperMonoBsp::releaseStorage() { return storage_.release(); }

#if defined(PAPERMONO_P3_FRONTLIGHT_BRINGUP)
bool PaperMonoBsp::prepareFrontlight() {
    if (!boardReady_) return false;

    // Keep the shared EPD/frontlight rail off until PM1 PWM is proven off and
    // the display reset line is confirmed asserted.
    if (!setFrontlightRail(false) || !assertFrontlightEpdReset() || !frontlight_.prepare()) return false;
    if (!frontlight_.pwmOff() || !frontlightEpdResetAsserted()) return false;
    if (!setFrontlightRail(true)) {
        abortFrontlight(false);
        return false;
    }
    return true;
}

bool PaperMonoBsp::setFrontlight(uint8_t percent) {
    if (percent == 0) return frontlightOff();
    if (!boardReady_ || !frontlightRailOn_ || !frontlightEpdResetAsserted()) {
        if (frontlightRailOn_) abortFrontlight(true);
        return false;
    }
    if (frontlight_.setPercent(percent)) return true;
    if (frontlight_.lastFailureWasCommunication()) {
        // PM1 cannot prove PWM state. Drop the shared rail before any further
        // PM1 operation, then reaffirm the already-required reset state.
        setFrontlightRail(false);
        assertFrontlightEpdReset();
    } else {
        abortFrontlight(true);
    }
    return false;
}

bool PaperMonoBsp::frontlightOff() {
    const bool off = frontlight_.off();
    if (!off && frontlightRailOn_) {
        if (frontlight_.lastFailureWasCommunication()) {
            setFrontlightRail(false);
            assertFrontlightEpdReset();
        } else {
            abortFrontlight(false);
        }
    }
    return off;
}

PaperMonoFrontlightReleaseResult PaperMonoBsp::releaseFrontlight() {
    PaperMonoFrontlightReleaseResult result;
    result.pwmOff = frontlight_.off();
    if (!result.pwmOff && frontlight_.lastFailureWasCommunication()) {
        result.railOff = setFrontlightRail(false);
        result.epdResetAsserted = assertFrontlightEpdReset();
    } else {
        result.epdResetAsserted = assertFrontlightEpdReset();
        result.railOff = setFrontlightRail(false);
    }
    if (result.railOff) result.pwmReleased = frontlight_.releaseLowPower();
    return result;
}

bool PaperMonoBsp::frontlightPwmOff() const { return frontlight_.pwmOff(); }

bool PaperMonoBsp::frontlightRailOn() const { return frontlightRailOn_; }

bool PaperMonoBsp::frontlightEpdResetAsserted() const {
    bool resetHigh = true;
    return freeink::m5ioe1::read(freeink::m5ioe1::PIN_EPD_RESET, &resetHigh) && !resetHigh;
}

bool PaperMonoBsp::setFrontlightRail(bool on) {
    if (!freeink::m5ioe1::write(freeink::m5ioe1::PIN_EPD_POWER, on)) return false;
    bool railHigh = !on;
    if (!freeink::m5ioe1::read(freeink::m5ioe1::PIN_EPD_POWER, &railHigh) || railHigh != on) return false;
    frontlightRailOn_ = on;
    return true;
}

bool PaperMonoBsp::assertFrontlightEpdReset() {
    if (!freeink::m5ioe1::write(freeink::m5ioe1::PIN_EPD_RESET, false)) return false;
    return frontlightEpdResetAsserted();
}

void PaperMonoBsp::abortFrontlight(bool attemptPwmOff) {
    if (attemptPwmOff && !frontlight_.off() && frontlight_.lastFailureWasCommunication()) {
        setFrontlightRail(false);
        assertFrontlightEpdReset();
        return;
    }
    assertFrontlightEpdReset();
    setFrontlightRail(false);
}
#endif

#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH)
PaperMonoNoRefreshServiceResult PaperMonoBsp::runNoRefreshPanelService() {
    PaperMonoNoRefreshServiceResult result;
    if (!boardReady_) return result;

    // This retains the proven PM1 PWM0-off write/readback semantics without
    // changing Frontlight configuration or G3 mux ownership.
    result.pwmOffPre = p4PwmOff();
    result.resetAsserted = p4SetReset(false);
    if (result.resetAsserted) {
        // Reference-derived reset-low interval before rail enable.
        delay(10);
    }
    result.spiInitialized = result.pwmOffPre && result.resetAsserted && display_.beginTransport();
    if (result.spiInitialized) {
        result.railOn = p4SetRail(true);
        if (result.railOn) {
            // Reference-derived rail stabilization delay.
            delay(10);
            result.resetReleased = p4SetReset(true);
            if (result.resetReleased) {
                // Reference-derived reset release delay.
                delay(10);
                result.busyIdlePre = display_.waitBusyIdle(15000);
                if (result.busyIdlePre) {
                    result.softwareReset = display_.softwareReset();
                    if (result.softwareReset) result.configured = display_.configureNoRefresh();
                }
            }
        }
    }

    // Containment is attempted exactly once regardless of bring-up outcome.
    result.pwmOffPost = p4PwmOff();
    result.resetSafePost = p4SetReset(false);
    result.railOff = p4SetRail(false);
    result.spiReleased = display_.releaseTransport();
    return result;
}
#endif

#if defined(PAPERMONO_P4_OTP_SINGLE_REFRESH)
PaperMonoOtpSingleRefreshResult PaperMonoBsp::runOtpSinglePanelService() {
    PaperMonoOtpSingleRefreshResult result;
    if (!boardReady_) return result;

    result.pwmOffPre = p4PwmOff();
    result.resetAsserted = p4SetReset(false);
    if (result.resetAsserted) {
        // Reference-derived reset-low interval before rail enable.
        delay(10);
    }
    result.spiInitialized = result.pwmOffPre && result.resetAsserted && display_.beginTransport();
    if (result.spiInitialized) {
        result.railOn = p4SetRail(true);
        if (result.railOn) {
            // Reference-derived rail stabilization delay.
            delay(10);
            result.resetReleased = p4SetReset(true);
            if (result.resetReleased) {
                // Reference-derived reset release delay.
                delay(10);
                result.busyIdlePre = display_.waitBusyIdle(15000);
                if (result.busyIdlePre) {
                    result.configured = display_.configureOtpMono();
                    if (result.configured) result.whiteBaselineWritten = display_.writeOtpWhiteBaseline();
                    if (result.whiteBaselineWritten)
                        result.frameWritten = display_.writeOtpInitialBlockFrame();
                    if (result.frameWritten) result.updateControl = display_.stageOtpUpdateControl();
                    if (result.updateControl)
                        result.busyDone = display_.activateOtpOnce(result.activationCount);
                }
            }
        }
    }

    // The P4 containment policy intentionally replaces OTP Demo deep sleep.
    result.pwmOffPost = p4PwmOff();
    result.resetSafePost = p4SetReset(false);
    result.railOff = p4SetRail(false);
    result.spiReleased = display_.releaseTransport();
    return result;
}
#endif

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_OTP_FULL_REFRESH) ||               \
    defined(PAPERMONO_P4_REPEATED_PARTIAL) || defined(PAPERMONO_P4_REFRESH_MANAGER)
PaperMonoOtpFullRefreshResult PaperMonoBsp::runOtpFullPanelService() {
    PaperMonoOtpFullRefreshResult result;
    if (!boardReady_) return result;

    result.pwmOffPre = p4PwmOff();
    result.resetAsserted = p4SetReset(false);
    if (result.resetAsserted) {
        // Reference-derived reset-low interval before rail enable.
        delay(10);
    }
    result.spiInitialized = result.pwmOffPre && result.resetAsserted && display_.beginTransport();
    if (result.spiInitialized) {
        result.railOn = p4SetRail(true);
        if (result.railOn) {
            // Reference-derived rail stabilization delay.
            delay(10);
            result.resetReleased = p4SetReset(true);
            if (result.resetReleased) {
                // Reference-derived reset release delay.
                delay(10);
                result.busyIdlePre = display_.waitBusyIdle(15000);
                if (result.busyIdlePre) {
                    result.configured = display_.configureOtpFullMono();
                    if (result.configured) result.stage1Control = display_.stageOtpFullFirstControl();
                    if (result.stage1Control) result.frameWritten = display_.writeOtpFullStageOneFrame();
                    if (result.frameWritten) {
                        result.stage1BusyDone = display_.activateOtpFullFirst(result.activationCount);
                        result.stage1Activated = result.activationCount == 1;
                    }
                    if (result.stage1BusyDone) result.stage2Control = display_.stageOtpFullSecondControl();
                    if (result.stage2Control) result.frameWritten = display_.writeOtpFullStageTwoFrames();
                    if (result.stage2Control && result.frameWritten) {
                        result.stage2BusyDone = display_.activateOtpFullSecond(result.activationCount);
                        result.stage2Activated = result.activationCount == 2;
                    }
                }
            }
        }
    }

    // The P4 containment policy intentionally replaces OTP Demo deep sleep.
    result.pwmOffPost = p4PwmOff();
    result.resetSafePost = p4SetReset(false);
    result.railOff = p4SetRail(false);
    result.spiReleased = display_.releaseTransport();
#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||               \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
    if (result.ok()) display_.seedOtpPreviousFromPending();
#endif
    return result;
}
#endif

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_DISPLAY_NO_REFRESH) ||             \
    defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) || defined(PAPERMONO_P4_OTP_FULL_REFRESH) ||                    \
    defined(PAPERMONO_P4_REPEATED_PARTIAL) || defined(PAPERMONO_P4_REFRESH_MANAGER)
bool PaperMonoBsp::p4PwmOff() {
    constexpr uint16_t kPwmEnableMask = 0x1000;
    if (!freeink::m5pm1::writeReg16(freeink::m5pm1::REG_PWM0_DUTY_L, 0)) return false;
    uint16_t value = 0;
    return freeink::m5pm1::readReg16(freeink::m5pm1::REG_PWM0_DUTY_L, &value) &&
           (value & kPwmEnableMask) == 0;
}

bool PaperMonoBsp::p4SetRail(bool on) {
    if (!freeink::m5ioe1::write(freeink::m5ioe1::PIN_EPD_POWER, on)) return false;
    bool railHigh = !on;
    return freeink::m5ioe1::read(freeink::m5ioe1::PIN_EPD_POWER, &railHigh) && railHigh == on;
}

bool PaperMonoBsp::p4SetReset(bool high) {
    if (!freeink::m5ioe1::write(freeink::m5ioe1::PIN_EPD_RESET, high)) return false;
    bool resetHigh = !high;
    return freeink::m5ioe1::read(freeink::m5ioe1::PIN_EPD_RESET, &resetHigh) && resetHigh == high;
}
#endif

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_REPEATED_PARTIAL) ||               \
    defined(PAPERMONO_P4_REFRESH_MANAGER)
#if defined(PAPERMONO_P4_REPEATED_PARTIAL) || defined(PAPERMONO_P4_REFRESH_MANAGER)
bool PaperMonoBsp::prepareRepeatedPartialTarget(bool inverse) {
    return display_.prepareOtpQuadrantTarget(inverse);
}
#endif

bool PaperMonoBsp::repeatedPartialShadowValid() const { return display_.otpPreviousFrameValid(); }

PaperMonoRepeatedPartialResult PaperMonoBsp::runOtpRepeatedPartialPanelService() {
    PaperMonoRepeatedPartialResult result;
    result.stateValid = boardReady_ && display_.otpPreviousFrameValid();
    if (!result.stateValid) return result;

    result.pwmOffPre = p4PwmOff();
    result.resetAsserted = p4SetReset(false);
    if (result.resetAsserted) delay(10);
    result.spiInitialized = result.pwmOffPre && result.resetAsserted && display_.beginTransport();
    if (result.spiInitialized) {
        result.railOn = p4SetRail(true);
        if (result.railOn) {
            delay(10);
            result.resetReleased = p4SetReset(true);
            if (result.resetReleased) {
                delay(10);
                result.busyIdlePre = display_.waitBusyIdle(15000);
                if (result.busyIdlePre) {
                    result.configured = display_.configureOtpMono();
                    if (result.configured) result.planesStaged = display_.writeOtpRepeatedPartialPlanes();
                    if (result.planesStaged) result.updateControl = display_.stageOtpUpdateControl();
                    if (result.updateControl)
                        result.busyDone = display_.activateOtpOnce(result.activationCount);
                    if (result.busyDone && result.activationCount == 1) {
                        result.shadowCommitted = display_.commitOtpPendingFrame();
                    }
                }
            }
        }
    }

    result.pwmOffPost = p4PwmOff();
    result.resetSafePost = p4SetReset(false);
    result.railOff = p4SetRail(false);
    result.spiReleased = display_.releaseTransport();
    return result;
}
#endif

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
bool PaperMonoBsp::submitMonochromeFrame(const uint8_t *packedFrame, size_t bytes) {
    if (!boardReady_) return false;
    return display_.submitMonochromeFrame(packedFrame, bytes);
}

bool PaperMonoBsp::submittedMonochromeFrameReady() const { return display_.pendingFrameValid(); }

PaperMonoRefreshResult PaperMonoBsp::requestRefresh(PaperMonoRefreshRequest request) {
    static PaperMonoRefreshManager manager(*this);
    return manager.request(request);
}
#endif

int PaperMonoBsp::batteryLevel() const {
    // Launcher treats 0 as the established unknown/unavailable battery sentinel.
    // A real PaperMono PM1 percentage remains deferred pending source-backed support.
    return 0;
}

void PaperMonoBsp::powerOff() {
#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND) || defined(PAPERMONO_P4_DISPLAY_NO_REFRESH) ||             \
    defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) || defined(PAPERMONO_P4_OTP_FULL_REFRESH) ||                    \
    defined(PAPERMONO_P4_REPEATED_PARTIAL) || defined(PAPERMONO_P4_REFRESH_MANAGER)
    (void)freeink::m5pm1::requestShutdown();
#else
    M5.Power.powerOff();
#endif
}
