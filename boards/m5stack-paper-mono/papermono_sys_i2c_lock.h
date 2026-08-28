#pragma once

// Board-local serialization for the PaperMono SYS-I2C Wire instance
// (G47/G48).  It protects complete logical operations, including consumption
// of TwoWire's shared receive buffer after requestFrom().

bool paperMonoSysI2cLockInit();

class PaperMonoSysI2cGuard {
public:
    PaperMonoSysI2cGuard();
    ~PaperMonoSysI2cGuard();

    PaperMonoSysI2cGuard(const PaperMonoSysI2cGuard &) = delete;
    PaperMonoSysI2cGuard &operator=(const PaperMonoSysI2cGuard &) = delete;

    bool locked() const;

private:
    bool locked_ = false;
};
