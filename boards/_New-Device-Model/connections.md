Here you briefly explain board pin definitions, PMIC, Gauge and Battery specs, Inputs, touch model, hardware sharing SPI and I2C Buses

# Example

## Modules sharing SPI bus 1
| Device  | SCK   | MISO  | MOSI  | CS    | GDO0/CE |
| ---     | :---: | :---: | :---: | :---: | :---:   |
| Display | 7     | 6     | 9     | 11    | -       |
| SDCard  | 7     | 6     | 9     | 27    | -       |
| CC1101  | 7     | 6     | 9     | 4     | 5       |


## Modules sharing SPI bus using GPIO bitbang
| Device  | SCK   | MISO  | MOSI  | CS    | GDO0/CE |
| ---     | :---: | :---: | :---: | :---: | :---:   |
| XPT2046 | 13    | 14    | 15    | 16    | -       |


## Modules sharing I2C bus
- I2C SDA: 2
- I2C SCL: 3

PN532 on I2C, PMIC BQ25896, Gauge BQ27220, RTC


## Device specific initialization
- GPIO `x` needs to be set High at start up to keep device working on battery only.
- All CS pins sharing SPI bus must be set HIGH before starting the bus.
- Set GPIO `y` LOW to keep board LED turned off.
- Other board specificities.

