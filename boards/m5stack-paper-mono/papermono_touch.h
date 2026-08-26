#pragma once

#include <cstdint>

struct PaperMonoTouchSample {
    bool touched = false;
    int16_t x = 0;
    int16_t y = 0;
};

// PaperMono-local FT6336G capability. The P2 bootstrap remains the sole owner
// of SYS I2C setup; this class only makes transactions on that established bus.
class PaperMonoTouch {
public:
    bool begin();
    bool ready() const;
    bool read(PaperMonoTouchSample &sample);

private:
    bool beginAttempted_ = false;
    bool ready_ = false;

    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegisters(uint8_t reg, uint8_t *data, uint8_t length);
};
