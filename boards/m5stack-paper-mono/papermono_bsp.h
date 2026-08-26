#pragma once

#include "papermono_bootstrap.h"
#include "papermono_touch.h"

class PaperMonoBsp {
public:
    static PaperMonoBsp &instance();

    void begin();
    bool boardReady() const;
    bool beginTouch();
    bool touchReady() const;
    bool readTouch(PaperMonoTouchSample &sample);
    int batteryLevel() const;
    void powerOff();

private:
    bool beginAttempted_ = false;
    bool boardReady_ = false;
    PaperMonoBootstrap bootstrap_;
    PaperMonoTouch touch_;
};
