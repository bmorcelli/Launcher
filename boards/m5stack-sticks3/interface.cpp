#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5PM1.h>
#include <Wire.h>
#include <interface.h>
#ifdef USE_CARDKB2
#include <cardkb2.h>
#endif

#define BTN_A_PIN 11
#define BTN_B_PIN 12

// --- M5PM1 power-management IC ------------------------------------------
#define PM1_SDA 47
#define PM1_SCL 48

static M5PM1 pm1;

void _setup_gpio() {
    if (pm1.begin(&Wire1, M5PM1_DEFAULT_ADDR, PM1_SDA, PM1_SCL) != M5PM1_OK) {
        launcherConsolePrintf("%s\n", String("M5PM1 init failed").c_str());
    }
    pm1.setChargeEnable(true);
    launcherDelayMs(20);
    pm1.setDcdcEnable(true);
    launcherDelayMs(20);
    pm1.setLdoEnable(true);
    launcherDelayMs(20);

    pm1.gpioSetFunc(M5PM1_GPIO_NUM_2, M5PM1_GPIO_FUNC_GPIO);
    pm1.gpioSetMode(M5PM1_GPIO_NUM_2, M5PM1_GPIO_MODE_OUTPUT);
    pm1.gpioSetDrive(M5PM1_GPIO_NUM_2, M5PM1_GPIO_DRIVE_PUSHPULL);
    pm1.gpioSetOutput(M5PM1_GPIO_NUM_2, HIGH);
    launcherDelayMs(20);

#ifndef USE_CARDKB2
    pm1.setBoostEnable(false);
#else
    pm1.setBoostEnable(true); // CardKB2 needs Grove 5V, energized directly at boot
    delay(100);
#endif
    /*
  | Device  | SCK   | MISO  | MOSI  | CS    | GDO0/CE   |
  | ---     | :---: | :---: | :---: | :---: | :---:     |
  | SD Card | 5     | 4     | 6     | 7     | ---       |
  | CC1101  | 5     | 4     | 6     | 2     | 3         |
  | NRF24   | 5     | 4     | 6     | 8     | 1         |
  | PN532   | 5     | 4     | 6     | 43    | --        |
  | WS500   | 5     | 4     | 6     | **    | **        |
  | LoRa    | 5     | 4     | 6     | **    | **        |
      */
    launcherGpioOutput(7);
    launcherGpioWrite(7, HIGH); // SD Card CS
    launcherGpioOutput(2);
    launcherGpioWrite(2, HIGH); // CC1101 CS
    launcherGpioOutput(8);
    launcherGpioWrite(8, HIGH); // nRF24L01 CS
    launcherGpioOutput(43);
    launcherGpioWrite(43, HIGH); // PN532 CS
    launcherGpioOutput(9);
    launcherGpioWrite(9, LOW); // M5RF433 avoid Jamming
    launcherGpioOutput(46);
    launcherGpioWrite(46, LOW); // Infrared LED Off

    hal_buttons_init_2(DeviceButtons{BTN_A_PIN, BTN_B_PIN}, 600);
}

void _post_setup_gpio() {
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);
}

void _late_setup_gpio() {
#ifdef USE_CARDKB2
    if (!CardKB2Installed) { pm1.setBoostEnable(false); }
#endif
}
void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

int getBattery() {
    uint16_t mv = 0;
    if (pm1.readVbat(&mv) != M5PM1_OK) return 0;
    int level = (int)(((float)mv - 3300.0f) * 100.0f / (4150.0f - 3350.0f));
    return (level < 0) ? 0 : (level >= 100) ? 100 : level;
}

void InputHandler(void) { hal_buttons_poll_2(); }

void powerOff() { pm1.shutdown(); }
