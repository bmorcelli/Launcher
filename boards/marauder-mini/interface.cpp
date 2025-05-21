#include <interface.h>
#include "powerSave.h"

#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <soc/soc_caps.h>
#include <soc/adc_channel.h>

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() { 
    pinMode(UP_BTN, INPUT);   // Sets the power btn as an INPUT
    pinMode(SEL_BTN, INPUT);
    pinMode(DW_BTN, INPUT);
    pinMode(R_BTN, INPUT);
    pinMode(L_BTN, INPUT);

}


/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() { 
    return 0;
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = millis();
    if(millis() - tm > 200 || LongPress) { }
    else return;

    bool u = digitalRead(UP_BTN);
    bool d = digitalRead(DW_BTN);
    bool r = digitalRead(R_BTN);
    bool l = digitalRead(L_BTN);
    bool s = digitalRead(SEL_BTN);
    if(s==BTN_ACT || u==BTN_ACT || d==BTN_ACT || r==BTN_ACT || l==BTN_ACT) {
        tm=millis();
        if(!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }    
    if(l==BTN_ACT) PrevPress = true;
    if(r==BTN_ACT) NextPress = true;
    if(u==BTN_ACT) UpPress = true;
    if(d==BTN_ACT) DownPress = true;
    if(s==BTN_ACT) SelPress = true;    
}


/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) { }

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { }


/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() { }