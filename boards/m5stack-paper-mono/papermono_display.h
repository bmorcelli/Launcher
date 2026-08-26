#pragma once

#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH) || defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) ||                  \
    defined(PAPERMONO_P4_OTP_FULL_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL)
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

#if defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL)
    bool configureOtpMono();
    bool writeOtpWhiteBaseline();
    bool writeOtpInitialBlockFrame();
    bool stageOtpUpdateControl();
    bool activateOtpOnce(uint8_t &activationCount);
#endif

#if defined(PAPERMONO_P4_OTP_FULL_REFRESH) || defined(PAPERMONO_P4_REPEATED_PARTIAL)
    bool configureOtpFullMono();
    bool stageOtpFullFirstControl();
    bool writeOtpFullStageOneFrame();
    bool activateOtpFullFirst(uint8_t &activationCount);
    bool stageOtpFullSecondControl();
    bool writeOtpFullStageTwoFrames();
    bool activateOtpFullSecond(uint8_t &activationCount);
#endif

#if defined(PAPERMONO_P4_REPEATED_PARTIAL)
    // The caller must seed this state only after a BUSY-completed full refresh.
    bool prepareOtpQuadrantTarget(bool inverse);
    bool seedOtpPreviousFromPending();
    bool otpPreviousFrameValid() const;
    bool writeOtpRepeatedPartialPlanes();
    bool commitOtpPendingFrame();
#endif

private:
    bool sendCommand(uint8_t command);
    bool sendCommandData(uint8_t command, const uint8_t *data, uint8_t length);
    bool transmitByte(uint8_t value, bool dataMode);
#if defined(PAPERMONO_P4_OTP_SINGLE_REFRESH) || defined(PAPERMONO_P4_OTP_FULL_REFRESH) ||                    \
    defined(PAPERMONO_P4_REPEATED_PARTIAL)
    bool sendDataBlock(const uint8_t *data, size_t length);
    bool setOtpFullWindow();
#endif

#if defined(PAPERMONO_P4_REPEATED_PARTIAL)
    bool ensureOtpShadowFrames();
    bool writeOtpFrameToRam(uint8_t command, const uint8_t *frame);
    void fillOtpQuadrantFrame(uint8_t *frame, bool inverse) const;

    uint8_t *otpPreviousFrame_ = nullptr;
    uint8_t *otpPendingFrame_ = nullptr;
    bool otpPreviousFrameValid_ = false;
#endif

    void *device_ = nullptr;
    bool busOwned_ = false;
};
#endif
