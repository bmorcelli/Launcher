#ifndef LAUNCHER_HAL_INPUTS_ENCODER_H
#define LAUNCHER_HAL_INPUTS_ENCODER_H

#include "../device.h"

// Rotary encoder HAL (RotaryEncoder lib) -- covers the LatchModes in use
// today: TWO03 (lilygo-t-embed-cc1101, m5stack-dinmeter, marauder-v4og's
// WAVESENTRY variant) and FOUR3 (lilygo-t-lora-pager, migrated in Etapa 7
// alongside its keyboard since the two are polled together there).
//
// cfg.pin_esc = -1 for a board with only one button on the encoder shaft
// (m5stack-dinmeter, lilygo-t-embed's "plain" variant); pass a real pin to
// also raise EscPress from a second dedicated button (lilygo-t-embed-cc1101's
// pinBack). Only one encoder instance is supported at a time (module-level
// state, matching every other hal_* poller) -- fine since no board has two.

enum class EncoderLatchMode : uint8_t { FOUR3, FOUR0, TWO03 };

// Allocates the RotaryEncoder, attaches its 2 pin-change interrupts, and
// configures pin_sel/pin_esc as input (pulled up per cfg.pullup). Call once
// from _setup_gpio().
void hal_encoder_init(const DeviceEncoder &cfg, EncoderLatchMode mode = EncoderLatchMode::TWO03);

// Translates accumulated rotation + pin_sel/pin_esc state into
// NextPress/PrevPress/SelPress/EscPress, replicating exactly the debounce
// (~200ms) and wakeUpScreen() gating every migrated board used to duplicate
// in its own InputHandler(). Call every InputHandler() cycle.
void hal_encoder_poll(const DeviceEncoder &cfg);

#endif
