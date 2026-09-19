// Native shim: no FreeRTOS on the desktop build. vTaskDelay maps onto the
// SDL simulator's own delay so a UI screen's timed redraws still pace
// themselves sanely instead of busy-looping. xTaskCreate spawns a real
// detached thread running the task function — main.cpp's taskInputHandler
// and taskSerialConsole are genuine infinite loops, so this is the one shim
// that needs to actually do something rather than no-op.
#pragma once

#include <cstdint>
#include <thread>

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

using TaskHandle_t = void *;
using TaskFunction_t = void (*)(void *);
using BaseType_t = int;
using UBaseType_t = unsigned;
using StackType_t = uint32_t;

#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) (ms)
#define pdTICKS_TO_MS(ticks) (ticks)
#define pdPASS 1

inline void vTaskDelay(uint32_t ticks) { lgfx::delay(ticks); }
inline void vTaskSuspend(TaskHandle_t) {}
inline void vTaskResume(TaskHandle_t) {}

inline BaseType_t xTaskCreate(
    TaskFunction_t task, const char *, uint32_t, void *param, UBaseType_t, TaskHandle_t *handle
) {
    std::thread(task, param).detach();
    if (handle) *handle = nullptr;
    return pdPASS;
}
