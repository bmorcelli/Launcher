#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

// ES3C28P — 2.8" ESP32-S3 IPS LCD Display Module (ESP32-S3R8)
// https://www.lcdwiki.com/2.8inch_ESP32-S3_Display

// SD card is native 4-bit SDIO, not SPI. Without this, globals.h's
// `#if !defined(SDM)` falls through and auto-defines SDM as plain SD plus
// SDM_SD, which silently routes sd_functions.cpp onto the SPI-mode SD path
// (with no CS pin ever defined for this board -> "IO 255" / f_mount
// failures) instead of SD_MMC. This must come before globals.h is first
// included anywhere in the build.
#define SDM SD_MMC

static const uint8_t LED_BUILTIN = 42; // WS2812/NeoPixel RGB LED (single wire)
#define BUILTIN_LED LED_BUILTIN
#define LED_BUILTIN LED_BUILTIN

// I2C0 — shared by FT6336G capacitive touch (0x38) and ES8311 audio codec (0x18)
static const uint8_t SDA = 16;
static const uint8_t SCL = 15;

// SPI — dedicated bus for the ILI9341V TFT (not shared with SD, which uses SDIO)
static const uint8_t SS = 10; // TFT_CS
static const uint8_t MOSI = 11;
static const uint8_t MISO = 13;
static const uint8_t SCK = 12;

// UART0
static const uint8_t TX = 43;
static const uint8_t RX = 44;

// USB D-/D+ (internal USB-serial)
static const uint8_t USB_DM = 19;
static const uint8_t USB_DP = 20;

// Battery ADC (voltage divider, read x2 for real voltage)
static const uint8_t A0 = 9;

#endif /* Pins_Arduino_h */
