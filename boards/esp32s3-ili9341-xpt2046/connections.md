# ESP32-S3 ILI9341 XPT2046 Board Pin Connections

## Display (ILI9341 SPI - SPI2_HOST)
- **SCLK (SCK)**: GPIO 12
- **MOSI (SDI)**: GPIO 11
- **MISO (SDO)**: GPIO 13
- **DC (Data/Command)**: GPIO 2
- **CS (Chip Select)**: GPIO 10
- **RST (Reset)**: GPIO 14
- **BL (Backlight PWM)**: GPIO 21

## Touchscreen (XPT2046 SPI - SPI3_HOST / Soft SPI)
- **T_CLK (SCLK)**: GPIO 6
- **T_MOSI (DIN)**: GPIO 5
- **T_MISO (DO)**: GPIO 4
- **T_CS**: GPIO 9
- **T_IRQ (INT)**: Unconnected (-1)
