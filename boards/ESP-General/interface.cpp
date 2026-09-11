#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>

#define BTN_ACT LOW
#ifdef CONFIG_IDF_TARGET_ESP32C5
#define SEL_BTN 28 // Using Boot button as Select button
#else
#define SEL_BTN 0 // Using Boot button as Select button
#endif

void _setup_gpio() {}

void _post_setup_gpio() {
    launcherConsolePrintf("%s\n", String("Setting GPIO 0 as Input, press to access the Launcher").c_str());
    launcherGpioInputPullup(SEL_BTN);
}

void _setBrightness(uint8_t brightval) {}

void InputHandler(void) {
    if (launcherGpioRead(SEL_BTN) == BTN_ACT) {
        SelPress = true;
        AnyKeyPress = true;
    }
}
