#pragma once

#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH) || defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) ||                  \
    defined(PAPERMONO_P4_OTP_FULL_REFRESH)
#include <stddef.h>
#include <stdint.h>

// SPI2-only controller transport for the P4 isolated gates. No raw controller
// command surface is exposed outside the board-local service boundary.
class PaperMonoDisplay {
public:
    bool beginTransport();
    bool waitBusyIdle(uint32_t timeoutMs);
    bool softwareReset();
    bool configureNoRefresh();
    bool releaseTransport();

#if defined(PAPERMONO_P4_OTP_SINGLE_REFRESH)
    bool configureOtpMono();
    bool writeOtpWhiteBaseline();
    bool writeOtpInitialBlockFrame();
    bool stageOtpUpdateControl();
    bool activateOtpOnce(uint8_t &activationCount);
#endif

#if defined(PAPERMONO_P4_OTP_FULL_REFRESH)
    bool configureOtpFullMono();
    bool stageOtpFullFirstControl();
    bool writeOtpFullStageOneFrame();
    bool activateOtpFullFirst(uint8_t &activationCount);
    bool stageOtpFullSecondControl();
    bool writeOtpFullStageTwoFrames();
    bool activateOtpFullSecond(uint8_t &activationCount);
#endif

private:
    bool sendCommand(uint8_t command);
    bool sendCommandData(uint8_t command, const uint8_t *data, uint8_t length);
    bool transmitByte(uint8_t value, bool dataMode);
#if defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) || defined(PAPERMONO_P4_OTP_FULL_REFRESH)
    bool sendDataBlock(const uint8_t *data, size_t length);
    bool setOtpFullWindow();
#endif

    void *device_ = nullptr;
    bool busOwned_ = false;
};
#endif
