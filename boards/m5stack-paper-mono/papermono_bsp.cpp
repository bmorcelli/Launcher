#include "papermono_bsp.h"

#include <M5Unified.h>

#if defined(PAPERMONO_P3_FRONTLIGHT_BRINGUP)
#include "vendor/freeink_board/M5Ioe1.h"
#endif

namespace {

#if defined(PAPERMONO_P2_NO_REFRESH_BOOTSTRAP)

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

#if defined(PAPERMONO_P2_NO_REFRESH_BOOTSTRAP)
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
#else
    stopP2SafeRuntime();
#endif
#else
    auto cfg = M5.config();
    cfg.clear_display = false;
    cfg.internal_mic = false;
    cfg.internal_spk = false;
    cfg.internal_imu = false;
#if defined(PAPERMONO_P2_BOOT_TELEMETRY)
    Serial.println("P27D_BEGIN");
#endif
    M5.begin(cfg);
#if defined(PAPERMONO_P2_BOOT_TELEMETRY)
    Serial.println("P27D_M5_BEGIN_RETURNED");
    Serial.printf("P27D_BOARD_PAPERMONO=%d\n", M5.getBoard() == m5::board_t::board_M5PaperMono);
    Serial.printf("P27D_DISPLAY_COUNT=%d\n", static_cast<int>(M5.getDisplayCount()));
    Serial.printf("P27D_PMIC_M5PM1=%d\n", M5.Power.getType() == m5::Power_Class::pmic_m5pm1);
    Serial.println("P27D_DONE");
#endif
    M5.Display.setAutoDisplay(false);

    boardReady_ = true;
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

int PaperMonoBsp::batteryLevel() const {
    const int percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

void PaperMonoBsp::powerOff() { M5.Power.powerOff(); }
