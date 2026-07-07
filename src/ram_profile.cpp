#include "ram_profile.h"

#ifdef ENABLE_RAM_LOGGING
#include <Arduino.h>
#include <esp_heap_caps.h>

void ramProfileLog(const char *tag) {
    Serial.printf(
        "[RAM] %s freeHeap=%u maxAlloc=%u internalFree=%u internalLargest=%u dmaFree=%u psramFree=%u\n",
        tag,
        static_cast<unsigned>(ESP.getFreeHeap()),
        static_cast<unsigned>(ESP.getMaxAllocHeap()),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
        static_cast<unsigned>(ESP.getFreePsram())
    );
}
#endif
