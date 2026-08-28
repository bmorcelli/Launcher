#include "papermono_frontlight.h"

#if defined(PAPERMONO_P3_FRONTLIGHT_BRINGUP)
#include "papermono_sys_i2c_lock.h"
#include "vendor/freeink_board/M5Pm1.h"

namespace {

constexpr uint8_t kPm1Gpio3 = 1u << 3;
constexpr uint8_t kPm1Gpio3FunctionMask = 0xC0;
constexpr uint8_t kPm1Gpio3PupdMask = 0xC0;
constexpr uint8_t kPm1RegGpioPupd0 = 0x14;
constexpr uint16_t kPwmFrequencyHz = 5000;
constexpr uint16_t kPwmDutyMask = 0x0FFF;
constexpr uint16_t kPwmEnableMask = 0x1000;

bool readPm1(uint8_t reg, uint8_t &value) { return freeink::m5pm1::readReg(reg, &value); }

} // namespace

bool PaperMonoFrontlight::prepare() {
    PaperMonoSysI2cGuard guard;
    if (!guard.locked()) return false;
    lastFailureWasCommunication_ = false;
    // The retained PM1 PWM state is made safe before changing the G3 mux.
    if (!off()) return false;

    if (!freeink::m5pm1::updateReg(freeink::m5pm1::REG_GPIO_DRV, kPm1Gpio3, 0) ||
        !freeink::m5pm1::updateReg(kPm1RegGpioPupd0, kPm1Gpio3PupdMask, 0) ||
        !freeink::m5pm1::updateReg(
            freeink::m5pm1::REG_GPIO_FUNC0, kPm1Gpio3FunctionMask, kPm1Gpio3FunctionMask
        ) ||
        !freeink::m5pm1::writeReg16(freeink::m5pm1::REG_PWM_FREQ_L, kPwmFrequencyHz)) {
        lastFailureWasCommunication_ = true;
        off();
        return false;
    }

    configured_ = verifyPrepared() && off();
    if (!configured_) lastFailureWasCommunication_ = true;
    return configured_;
}

bool PaperMonoFrontlight::setPercent(uint8_t percent) {
    PaperMonoSysI2cGuard guard;
    if (!guard.locked()) return false;
    lastFailureWasCommunication_ = false;
    if (percent == 0) return off();
    if (!configured_) return false;
    if (percent > 100) percent = 100;

    const uint16_t brightness = static_cast<uint16_t>(percent) * 255u / 100u;
    uint16_t duty = static_cast<uint16_t>((brightness * brightness) / 16u);
    if (duty > kPwmDutyMask) duty = kPwmDutyMask;

    const uint16_t enabledDuty = static_cast<uint16_t>(duty | kPwmEnableMask);
    if (!freeink::m5pm1::writeReg16(freeink::m5pm1::REG_PWM0_DUTY_L, enabledDuty)) {
        lastFailureWasCommunication_ = true;
        return false;
    }

    uint16_t verify = 0;
    if (!freeink::m5pm1::readReg16(freeink::m5pm1::REG_PWM0_DUTY_L, &verify)) {
        lastFailureWasCommunication_ = true;
        return false;
    }
    return verify == enabledDuty;
}

bool PaperMonoFrontlight::off() {
    PaperMonoSysI2cGuard guard;
    if (!guard.locked()) return false;
    lastFailureWasCommunication_ = false;
    if (!freeink::m5pm1::writeReg16(freeink::m5pm1::REG_PWM0_DUTY_L, 0)) {
        lastFailureWasCommunication_ = true;
        return false;
    }

    uint16_t value = 0;
    if (!freeink::m5pm1::readReg16(freeink::m5pm1::REG_PWM0_DUTY_L, &value)) {
        lastFailureWasCommunication_ = true;
        return false;
    }
    return (value & kPwmEnableMask) == 0;
}

bool PaperMonoFrontlight::pwmOff() const {
    PaperMonoSysI2cGuard guard;
    if (!guard.locked()) return false;
    uint16_t value = 0;
    return freeink::m5pm1::readReg16(freeink::m5pm1::REG_PWM0_DUTY_L, &value) &&
           (value & kPwmEnableMask) == 0;
}

bool PaperMonoFrontlight::lastFailureWasCommunication() const { return lastFailureWasCommunication_; }

bool PaperMonoFrontlight::releaseLowPower() {
    PaperMonoSysI2cGuard guard;
    if (!guard.locked()) return false;
    if (!pwmOff()) return false;

    bool released = freeink::m5pm1::updateReg(freeink::m5pm1::REG_GPIO_FUNC0, kPm1Gpio3FunctionMask, 0);
    released &= freeink::m5pm1::updateReg(freeink::m5pm1::REG_GPIO_MODE, kPm1Gpio3, 0);
    released &= freeink::m5pm1::updateReg(kPm1RegGpioPupd0, kPm1Gpio3PupdMask, 0);
    released &= freeink::m5pm1::updateReg(freeink::m5pm1::REG_GPIO_DRV, 0, kPm1Gpio3);
    if (!released) return false;

    uint8_t function = 0;
    uint8_t mode = 0;
    uint8_t pull = 0;
    uint8_t drive = 0;
    const bool verified = readPm1(freeink::m5pm1::REG_GPIO_FUNC0, function) &&
                          readPm1(freeink::m5pm1::REG_GPIO_MODE, mode) && readPm1(kPm1RegGpioPupd0, pull) &&
                          readPm1(freeink::m5pm1::REG_GPIO_DRV, drive) &&
                          (function & kPm1Gpio3FunctionMask) == 0 && (mode & kPm1Gpio3) == 0 &&
                          (pull & kPm1Gpio3PupdMask) == 0 && (drive & kPm1Gpio3) != 0;
    if (verified) configured_ = false;
    return verified;
}

bool PaperMonoFrontlight::verifyPrepared() const {
    uint8_t drive = 0;
    uint8_t function = 0;
    uint8_t pull = 0;
    uint16_t frequency = 0;
    return readPm1(freeink::m5pm1::REG_GPIO_DRV, drive) &&
           readPm1(freeink::m5pm1::REG_GPIO_FUNC0, function) && readPm1(kPm1RegGpioPupd0, pull) &&
           freeink::m5pm1::readReg16(freeink::m5pm1::REG_PWM_FREQ_L, &frequency) &&
           (drive & kPm1Gpio3) == 0 && (function & kPm1Gpio3FunctionMask) == kPm1Gpio3FunctionMask &&
           (pull & kPm1Gpio3PupdMask) == 0 && frequency == kPwmFrequencyHz;
}
#endif
