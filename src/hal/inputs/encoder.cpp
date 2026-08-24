#include "encoder.h"

#include "globals.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"

// RotaryEncoder isn't in every board's lib_deps -- only boards that set
// HAS_ENCODER pull it in, so PlatformIO's LDF doesn't need to find it for
// everyone else.
#if defined(HAS_ENCODER)
#include <RotaryEncoder.h>

static RotaryEncoder *halEncoder = nullptr;

static IRAM_ATTR void halEncoderTick() { halEncoder->tick(); }

static RotaryEncoder::LatchMode toLibMode(EncoderLatchMode mode) {
    switch (mode) {
        case EncoderLatchMode::FOUR3: return RotaryEncoder::LatchMode::FOUR3;
        case EncoderLatchMode::FOUR0: return RotaryEncoder::LatchMode::FOUR0;
        default: return RotaryEncoder::LatchMode::TWO03;
    }
}

void hal_encoder_init(const DeviceEncoder &cfg, EncoderLatchMode mode) {
    if (cfg.pin_sel >= 0) {
        if (cfg.pullup) launcherGpioInputPullup(cfg.pin_sel);
        else launcherGpioInput(cfg.pin_sel);
    }
    if (cfg.pin_esc >= 0) {
        if (cfg.pullup) launcherGpioInputPullup(cfg.pin_esc);
        else launcherGpioInput(cfg.pin_esc);
    }

    halEncoder = new RotaryEncoder(cfg.pin_a, cfg.pin_b, toLibMode(mode));
    attachInterrupt(digitalPinToInterrupt(cfg.pin_a), halEncoderTick, CHANGE);
    attachInterrupt(digitalPinToInterrupt(cfg.pin_b), halEncoderTick, CHANGE);
}

void hal_encoder_poll(const DeviceEncoder &cfg) {
    static unsigned long tm = 0;
    static int posDifference = 0;
    static long lastPos = 0;

    long newPos = halEncoder->getPosition();
    if (newPos != lastPos) {
        posDifference += (newPos - lastPos);
        lastPos = newPos;
    }

    if (launcherMillis() - tm < 200 && !LongPress) return;

    bool sel = cfg.pin_sel >= 0 && launcherGpioRead(cfg.pin_sel) == LOW;
    bool esc = cfg.pin_esc >= 0 && launcherGpioRead(cfg.pin_esc) == LOW;

    if (posDifference != 0 || sel || esc) {
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }
    if (posDifference > 0) {
        PrevPress = true;
        posDifference--;
    }
    if (posDifference < 0) {
        NextPress = true;
        posDifference++;
    }

    if (sel) {
        posDifference = 0;
        SelPress = true;
        tm = launcherMillis();
    }
    if (esc) {
        EscPress = true;
        tm = launcherMillis();
    }
}
#else
void hal_encoder_init(const DeviceEncoder &cfg, EncoderLatchMode mode) {
    (void)cfg;
    (void)mode;
}
void hal_encoder_poll(const DeviceEncoder &cfg) { (void)cfg; }
#endif
