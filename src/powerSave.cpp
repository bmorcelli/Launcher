#include "powerSave.h"
#include "settings.h"

/* Turn off the display */
void turnOffDisplay() { setBrightness(0, false); }

/* If the device screen is off, turn on, else just refresh sleep timer */
bool wakeUpScreen() {
    previousMillis = millis();
    if (isScreenOff) {
        isScreenOff = false;
        dimmer = false;
        getBrightness();
        return true;
    } else if (dimmer) {
        dimmer = false;
        getBrightness();
        return true;
    }
    return false;
}

/* Check if it's time to put the device to sleep */
void checkPowerSaveTime() {
    if (dimmerSet != 0) {
        if ((millis() - previousMillis) >= (dimmerSet * 1000) && dimmer == false && isSleeping == false) {
            dimmer = true;
            setBrightness(5, false);
        } else if ((millis() - previousMillis) >= ((dimmerSet * 1000) + 5000) && isScreenOff == false &&
                   isSleeping == false) {
            isScreenOff = true;
            turnOffDisplay();
        }
    }
}

/* Put device on sleep mode */
void sleepModeOn() {
    isSleeping = true;
#ifndef CONFIG_IDF_TARGET_ESP32P4
    setCpuFrequencyMhz(80);
    turnOffDisplay();
    disableCore0WDT();
    disableCore1WDT();
    disableLoopWDT();
#endif
}

/* Wake up device */
void sleepModeOff() {
    isSleeping = false;
#ifndef CONFIG_IDF_TARGET_ESP32P4
    setCpuFrequencyMhz(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    enableCore0WDT();
    enableCore1WDT();
    enableLoopWDT();
    feedLoopWDT();
#endif
    getBrightness();
}
