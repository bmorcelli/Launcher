# Heltec WiFi LoRa 32 Expansion Kit V2

This target pairs the V4-R8 (ESP32-S3, 16 MB flash, 8 MB octal PSRAM) with the V2 touchscreen and microSD expansion. Select the full kit name when choosing firmware. The V1 kit and the bare V4 board have different peripherals.

## Install Launcher

1. Back up the current firmware before changing the partition table. With esptool 5 and the kit connected by USB, run `esptool --chip esp32s3 --port PORT read-flash 0 0x1000000 before-launcher.bin`. Check the copy with `esptool --chip esp32s3 --port PORT verify-flash 0 before-launcher.bin`. Substitute the kit's actual serial port. Keep this full-chip backup private.
2. From a recursive checkout of Launcher, run `pio run -e heltec-lora32-expansion-v2`. The merged installer appears at the root of the checkout.
3. Run `esptool --chip esp32s3 --port PORT write-flash 0 Launcher-heltec-lora32-expansion-v2.bin`. This replaces the bootloader, partition table and current application. If automatic download mode fails, hold USER, tap reset, then release USER. Leave IO released during USB flashing.
4. In Launcher, open **SD**, choose an application binary built for this exact kit and select **Install**. The serial installer in `tools/serial_flasher.py` also accepts an application image. The online installer uses the separate LauncherHub category `heltec_lora32_expansion_v2`. Firmware appears there when the catalog operator provisions that category and lists compatible builds.

To return the device to a verified backup, write the full-chip backup at address zero using the same port and `write-flash --flash-mode keep --flash-size keep --flash-freq keep`. This restores the internal flash contents as of the backup.

## Controls

| Control | Action |
| --- | --- |
| Touchscreen | Tap a menu item or the navigation strip. Tap the center of the startup screen to enter Launcher. |
| USER (GPIO0) | Click for Next. Hold for 600 ms or double-click for Select. |
| IO (GPIO46) | Click for Previous. Hold for 600 ms or double-click for Back. |
| OFF | Enter deep sleep. Press USER to wake. The physical power switch controls the battery supply. |

If enabling **GPIO on boot starts Launcher** in Settings, choose **IO** and set **GPIO Level** to **1**. The board pulls this input low at rest. Battery percentage is an estimate from the cell voltage.

## Expansion connections

| Function | ESP32-S3 GPIO |
| --- | --- |
| TFT and microSD SPI | SCLK 16, MOSI 15, MISO 45. TFT CS 47, SD CS 3. |
| ST7789P3 TFT | D/C 48, reset 21, backlight PWM 44. Native 240 x 320. |
| CHSC6xxx touch | SDA 17, SCL 18, address 0x2E. The TFT resets both controllers through GPIO21. Touch is polled. |
| Buttons | USER 0 active low. IO 46 active high. |
| Expansion supply | GPIO40 active low, powers display, SD and touch pull-ups. |
| Battery divider | GPIO1, 390 kOhm over 100 kOhm. |
| GNSS | GPIO42 active low. Launcher powers down the main GNSS supply. |
| LoRa and buzzer | Radio CS 8 high, RF enable 2 and RF supply 7 low, buzzer 4 low while in Launcher. Installed apps initialize these peripherals themselves. |

See the [kit specifications](https://heltec.org/project/v4-r8-ex/), [V4-R8 mainboard schematic](https://resource.heltec.cn/download/WiFi_LoRa_32_V4-R8/Schematic/HTIT-WBR8H_V4.3.2.pdf) and [V2.03 expansion schematic](https://resource.heltec.cn/download/WiFi_LoRa_32_Expansion_Kit-V2/schematic/Expansion_board_V2.03.pdf).
