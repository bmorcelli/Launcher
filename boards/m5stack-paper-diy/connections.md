M5Stack PaperDIY (K159 Assembly Kit / K159-B Complete Kit): ESP32-S3-WROOM-1-N16R8
(16 MB flash, 8 MB octal PSRAM), 4.7" 960x540 e-paper (ET047TC1), no touch.
Docs: https://docs.m5stack.com/en/products/sku/K159-B

## Display (8-bit parallel EPD, driven by M5GFX, same bus code as PaperS3)
| Signal | DB0-DB7                       | XCL | XLE | XSTL | CKV | SPV | BST_EN |
| ---    | :---:                         | :---: | :---: | :---: | :---: | :---: | :---: |
| GPIO   | 6, 14, 7, 12, 9, 11, 8, 10    | 16  | 15  | 13   | 18  | 17  | 46     |

Panel power (EPD_PWR) is M5PM1 GPIO2; M5Unified enables it in `M5.begin()`.

## Modules sharing SPI bus
| Device                    | SCK   | MISO  | MOSI  | CS    | Other |
| ---                       | :---: | :---: | :---: | :---: | :---: |
| SDCard                    | 39    | 40    | 38    | 47    | DET 48 |
| FPC-SPI expansion (12P)   | 39    | 40    | 38    | 21    | BUSY 43, IRQ 44, RST/SW via M5PM1 GPIO4/GPIO1 |

Same SD pins as the PaperS3.

## Modules sharing I2C bus
- I2C SDA: 41
- I2C SCL: 42

M5PM1 PMIC (0x6E) -- board autodetection, battery voltage, CHG_STAT on M5PM1 GPIO3
(low = charging), power off. Also routed to the FPC-I2S expansion connector.

## Inputs
- BtnA: GPIO 4 (active low) -> Next / Sel
- BtnB: GPIO 3 (active low) -> Prev / Esc
- Power button: handled by the M5PM1 (click = on/reset, double click = off,
  ~2 s hold = download mode, red LED blinks).
- Hold A or B while booting to start Launcher (`LAUNCHER_GPIO_KEYS`).

## Device specific initialization
- 140 mAh Li-Po on SH1.0-2P; LGS4056H charger.
- Requires M5Unified >= 0.2.28 / M5GFX >= 0.2.35 (first versions with `board_M5PaperDIY`).
