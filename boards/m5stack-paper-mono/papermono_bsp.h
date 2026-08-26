#pragma once

#include "papermono_bootstrap.h"

class PaperMonoBsp {
public:
    static PaperMonoBsp &instance();

    void begin();
    bool boardReady() const;
    int batteryLevel() const;
    void powerOff();

private:
    bool beginAttempted_ = false;
    bool boardReady_ = false;
    PaperMonoBootstrap bootstrap_;
};
