#ifndef LAUNCHER_PLATFORM_H
#define LAUNCHER_PLATFORM_H

#include "driver/gpio.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>

inline uint32_t launcherMillis() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }

inline void launcherDelayMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

inline uint32_t launcherRandom(uint32_t max) { return max == 0 ? 0 : esp_random() % max; }

inline uint32_t launcherRandom(uint32_t min, uint32_t max) {
    return min >= max ? min : min + launcherRandom(max - min);
}

inline void launcherGpioOutput(int pin) {
    if (pin >= 0) gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
}

inline void launcherGpioInput(int pin) {
    if (pin >= 0) gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT);
}

inline void launcherGpioInputPullup(int pin) {
    if (pin < 0) return;
    auto gpio = static_cast<gpio_num_t>(pin);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);
}

inline int launcherGpioRead(int pin) { return pin >= 0 ? gpio_get_level(static_cast<gpio_num_t>(pin)) : 0; }

inline void launcherGpioWrite(int pin, int level) {
    if (pin >= 0) gpio_set_level(static_cast<gpio_num_t>(pin), level ? 1 : 0);
}

void launcherConsolePrintf(const char *fmt, ...);
void launcherConsolePrint(const char *text);
void launcherConsolePrintln(const char *text);
void launcherConsoleBegin(unsigned long baud);
void launcherConsoleFlush();
void launcherConsoleEnd();

#endif
