#include "papermono_sys_i2c_lock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

// PaperMonoBsp::begin() initializes this before taskInputHandler is created.
SemaphoreHandle_t gPaperMonoSysI2cLock = nullptr;

} // namespace

bool paperMonoSysI2cLockInit() {
    if (gPaperMonoSysI2cLock != nullptr) return true;
    gPaperMonoSysI2cLock = xSemaphoreCreateRecursiveMutex();
    return gPaperMonoSysI2cLock != nullptr;
}

PaperMonoSysI2cGuard::PaperMonoSysI2cGuard() {
    if (gPaperMonoSysI2cLock == nullptr) return;
    locked_ = xSemaphoreTakeRecursive(gPaperMonoSysI2cLock, portMAX_DELAY) == pdTRUE;
}

PaperMonoSysI2cGuard::~PaperMonoSysI2cGuard() {
    if (locked_) xSemaphoreGiveRecursive(gPaperMonoSysI2cLock);
}

bool PaperMonoSysI2cGuard::locked() const { return locked_; }
