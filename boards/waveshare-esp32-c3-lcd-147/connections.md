# Waveshare ESP32-C3 LCD 1.47

The ESP32-C3FH4 has 4 MB flash. The ST7789 display uses SPI clock GPIO 7, MOSI GPIO 5, and DC GPIO 8. The BOOT button is GPIO 9.

The CH32V003 I/O expander at I²C address 0x24 uses SDA GPIO 4 and SCL GPIO 3. Its outputs 0, 1, and 2 control display chip select, display reset, and SD card chip select, respectively. Its PWM register controls the display backlight. The display and SD card share SPI; SD MISO is GPIO 6. The board-specific build script adds expander chip-select hooks to the cached Arduino SD driver, gated by a board build flag. The shared SPI lock keeps display transfers out of SD transactions, and the expander needs a 10 ms delay after switching chip selects.

Pin assignments and display geometry follow the [Waveshare example](https://github.com/waveshareteam/ESP32-C3-LCD-1.47).
