

#ifndef __MASS_STORAGE_H__
#define __MASS_STORAGE_H__

#include <globals.h>
#if defined(SOC_USB_OTG_SUPPORTED) && !defined(DISABLE_MASS_STORAGE)
// All credits to @geo_tp for his incredible POC https://github.com/geo-tp/Esp32-USB-Stick
// Many thanks to @R3cK for bringing it to Bruce, and allowing me to bring it to Launcher

class MassStorage {
public:
    static bool shouldStop;

    /////////////////////////////////////////////////////////////////////////////////////
    // Constructor
    /////////////////////////////////////////////////////////////////////////////////////
    MassStorage();
    ~MassStorage();

    /////////////////////////////////////////////////////////////////////////////////////
    // Life Cycle
    /////////////////////////////////////////////////////////////////////////////////////
    void setup();
    void loop();

    /////////////////////////////////////////////////////////////////////////////////////
    // Operations
    /////////////////////////////////////////////////////////////////////////////////////
    static void setShouldStop(bool value) { shouldStop = value; }

    /////////////////////////////////////////////////////////////////////////////////////
    // Display functions
    /////////////////////////////////////////////////////////////////////////////////////
    static void displayMessage(String message);

private:
    void beginUsb(void);
};

int32_t usbWriteCallback(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize);
int32_t usbReadCallback(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize);
bool usbStartStopCallback(uint8_t power_condition, bool start, bool load_eject);

void drawUSBStickIcon(bool plugged);

#endif // SOC_USB_OTG_SUPPORTED && !DISABLE_MASS_STORAGE
#endif // MASS_STORAGE_H
