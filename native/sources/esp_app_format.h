// Native shim: layout-compatible enough for sizeof()/field access; nothing
// here actually parses a real ESP32 app image on this harness.
#pragma once

#include <cstdint>

#define ESP_IMAGE_HEADER_MAGIC 0xE9
#define ESP_IMAGE_MAX_SEGMENTS 16
#define ESP_IMAGE_HASH_LEN 32

typedef struct {
    uint8_t magic;
    uint8_t segment_count;
    uint8_t spi_mode;
    uint8_t spi_speed_size;
    uint32_t entry_addr;
    uint8_t wp_pin;
    uint8_t spi_pin_drv[3];
    uint16_t chip_id;
    uint8_t min_chip_rev;
    uint16_t min_chip_rev_full;
    uint16_t max_chip_rev_full;
    uint8_t reserv[4];
    uint8_t hash_appended;
} esp_image_header_t;

typedef struct {
    uint32_t load_addr;
    uint32_t data_len;
} esp_image_segment_header_t;
