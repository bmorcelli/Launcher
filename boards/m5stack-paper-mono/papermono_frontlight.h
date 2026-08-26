#pragma once

#include <stdint.h>

class PaperMonoFrontlight {
public:
    // Configure PM1 G3/PWM0 at 5 kHz while leaving PWM0 disabled.
    bool prepare();
    bool setPercent(uint8_t percent);
    bool off();
    bool pwmOff() const;
    bool lastFailureWasCommunication() const;

    // Call only after the BSP has verified that the shared rail is off.
    bool releaseLowPower();

private:
    bool verifyPrepared() const;

    bool configured_ = false;
    bool lastFailureWasCommunication_ = false;
};
