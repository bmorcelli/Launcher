#include <Arduino.h>
#include <esp_rom_uart.h>
#include <esp_rom_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef PAPERMONO_SAFE_BRINGUP
#error "safe_bringup.cpp must only be built with PAPERMONO_SAFE_BRINGUP enabled"
#endif

static void romUsbPuts(const char *text) {
    while (*text != '\0') { esp_rom_output_putc(*text++); }
}

void setup() {
    delay(1000);
    esp_rom_output_set_as_console(ESP_ROM_USB_SERIAL_DEVICE_NUM);
    romUsbPuts("PAPERMONO ROM USB SAFE BRING-UP\r\n");
    romUsbPuts("NO PAPERMONO PERIPHERALS INITIALIZED\r\n");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(5000));
    romUsbPuts("PAPERMONO ROM USB HEARTBEAT\r\n");
}
