#include <Arduino.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef PAPERMONO_SAFE_BRINGUP
#error "safe_bringup.cpp must only be built with PAPERMONO_SAFE_BRINGUP enabled"
#endif

void setup() {
    HWCDCSerial.begin(115200);

    const uint32_t serialWaitStarted = millis();
    while (!HWCDCSerial && (millis() - serialWaitStarted < 8000U)) { delay(20); }
    if (HWCDCSerial) { delay(250); }

    HWCDCSerial.println();
    HWCDCSerial.println("PAPERMONO SAFE BRING-UP STAGE 1.1");
    HWCDCSerial.printf("Chip model: %s\r\n", ESP.getChipModel());
    HWCDCSerial.printf("Chip revision: %u\r\n", ESP.getChipRevision());
    HWCDCSerial.printf("CPU frequency: %lu MHz\r\n", static_cast<unsigned long>(ESP.getCpuFreqMHz()));
    HWCDCSerial.printf("Flash size: %lu bytes\r\n", static_cast<unsigned long>(ESP.getFlashChipSize()));
    HWCDCSerial.printf("PSRAM size: %lu bytes\r\n", static_cast<unsigned long>(ESP.getPsramSize()));
    HWCDCSerial.printf("Free heap: %lu bytes\r\n", static_cast<unsigned long>(ESP.getFreeHeap()));
    HWCDCSerial.printf("Reset reason: %d\r\n", static_cast<int>(esp_reset_reason()));
    HWCDCSerial.println("NO M5Unified INIT");
    HWCDCSerial.println("NO DISPLAY INIT");
    HWCDCSerial.println("NO SD INIT");
    HWCDCSerial.println("NO PMIC/IOE1 COMMANDS");
    HWCDCSerial.flush();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(5000));
    HWCDCSerial.println("PAPERMONO SAFE HEARTBEAT");
    HWCDCSerial.flush();
}
