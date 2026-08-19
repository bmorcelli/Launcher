// Native shim: no real GPIO on the desktop build, every call is a no-op.
#pragma once

#include <cstdint>

typedef int gpio_num_t;
#define GPIO_NUM_NC -1

enum gpio_mode_t { GPIO_MODE_INPUT, GPIO_MODE_OUTPUT };
enum gpio_pull_mode_t { GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY, GPIO_FLOATING };

inline int gpio_set_direction(gpio_num_t, gpio_mode_t) { return 0; }
inline int gpio_set_pull_mode(gpio_num_t, gpio_pull_mode_t) { return 0; }
inline int gpio_get_level(gpio_num_t) { return 0; }
inline int gpio_set_level(gpio_num_t, uint32_t) { return 0; }
