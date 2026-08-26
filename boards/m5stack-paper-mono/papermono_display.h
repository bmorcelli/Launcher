#pragma once

#if defined(PAPERMONO_P4_DISPLAY_NO_REFRESH)
#include <stdint.h>

// SPI2-only controller transport for the P4 isolated gate. This interface
// intentionally exposes only no-refresh controller setup and containment.
class PaperMonoDisplay {
public:
    bool beginTransport();
    bool waitBusyIdle(uint32_t timeoutMs);
    bool softwareReset();
    bool configureNoRefresh();
    bool releaseTransport();

private:
    bool sendCommand(uint8_t command);
    bool sendCommandData(uint8_t command, const uint8_t *data, uint8_t length);
    bool transmitByte(uint8_t value, bool dataMode);

    void *device_ = nullptr;
    bool busOwned_ = false;
};
#endif
