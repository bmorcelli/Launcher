#include "papermono_touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "papermono_sys_i2c_lock.h"
#include "vendor/freeink_board/M5Ioe1.h"

namespace {

// FT6336G is FT5x06-compatible. Register definitions and polling mode follow
// the pinned M5GFX Touch_FT5x06 implementation.
constexpr uint8_t kFt6336Address = 0x38;
constexpr uint8_t kWorkingModeRegister = 0x00;
constexpr uint8_t kChipIdRegister = 0xA3;
constexpr uint8_t kInterruptModeRegister = 0xA4;
constexpr uint8_t kTouchCountRegister = 0x02;
constexpr uint8_t kPollingMode = 0x00;

constexpr uint32_t kTouchResetAssertMs = 8;
constexpr uint32_t kTouchResetReleaseMs = 10;
constexpr uint8_t kInitAttempts = 12;
constexpr uint32_t kInitRetryDelayMs = 50;

} // namespace

bool PaperMonoTouch::begin() {
    if (beginAttempted_) return ready_;
    beginAttempted_ = true;

    // This sequence is the local PaperMono reference's enableTouch(): power
    // TP_VDD, assert TP_RST, then release it after the recorded delays.
    {
        PaperMonoSysI2cGuard guard;
        if (!guard.locked() || !freeink::m5ioe1::write(freeink::m5ioe1::PIN_TOUCH_POWER, true) ||
            !freeink::m5ioe1::write(freeink::m5ioe1::PIN_TOUCH_RESET, false)) {
            return false;
        }
    }
    delay(kTouchResetAssertMs);
    {
        PaperMonoSysI2cGuard guard;
        if (!guard.locked() || !freeink::m5ioe1::write(freeink::m5ioe1::PIN_TOUCH_RESET, true)) return false;
    }
    delay(kTouchResetReleaseMs);

    // Polling is sufficient for the minimal bring-up. GPIO4 is configured as
    // the controller's pull-up interrupt input but no ISR/wake path is added.
    pinMode(4, INPUT_PULLUP);

    // The PaperMono FreeInk reference enters working mode, reads 0xA3..0xA8,
    // then selects polling mode. It retries this exact transaction sequence
    // because the controller can still be starting after reset. Do not test
    // the returned ID bytes: the reference records valid PaperMono units that
    // serve this window as zeroes.
    uint8_t chipIdentity[6] = {};
    for (uint8_t attempt = 0; attempt < kInitAttempts && !ready_; ++attempt) {
        if (attempt != 0) delay(kInitRetryDelayMs);
        ready_ = writeRegister(kWorkingModeRegister, kPollingMode) &&
                 readRegisters(kChipIdRegister, chipIdentity, sizeof(chipIdentity)) &&
                 writeRegister(kInterruptModeRegister, kPollingMode);
    }
    return ready_;
}

bool PaperMonoTouch::ready() const { return ready_; }

bool PaperMonoTouch::read(PaperMonoTouchSample &sample) {
    sample = {};
    if (!ready_) return false;

    uint8_t data[5] = {};
    if (!readRegisters(kTouchCountRegister, data, sizeof(data))) return false;
    if ((data[0] & 0x0F) == 0) return true;

    sample.touched = true;
    sample.x = static_cast<int16_t>(((data[1] & 0x0F) << 8) | data[2]);
    sample.y = static_cast<int16_t>(((data[3] & 0x0F) << 8) | data[4]);
    return true;
}

bool PaperMonoTouch::writeRegister(uint8_t reg, uint8_t value) {
    PaperMonoSysI2cGuard guard;
    if (!guard.locked()) return false;
    Wire.beginTransmission(kFt6336Address);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool PaperMonoTouch::readRegisters(uint8_t reg, uint8_t *data, uint8_t length) {
    PaperMonoSysI2cGuard guard;
    if (!guard.locked()) return false;
    Wire.beginTransmission(kFt6336Address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(kFt6336Address, length) != length) return false;
    for (uint8_t index = 0; index < length; ++index) data[index] = Wire.read();
    return true;
}
