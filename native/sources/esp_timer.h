// Native shim.
#pragma once

#include <cstdint>

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

inline int64_t esp_timer_get_time() { return static_cast<int64_t>(lgfx::micros()); }
