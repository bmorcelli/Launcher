#include <interface.h>
#include "powerSave.h"

#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <soc/soc_caps.h>
#include <soc/adc_channel.h>
#include <CYD28_TouchscreenR.h>

CYD28_TouchR touch(320, 240);

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio()
{
  pinMode(TFT_BL, OUTPUT);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio()
{
  if (!touch.begin(&SPI))
  {
    Serial.println("Touch IC not Started");
    log_i("Touch IC not Started");
  }
  else
    Serial.println("Touch IC Started");
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval)
{
  if (brightval > 5)
    digitalWrite(TFT_BL, HIGH);
  else
    digitalWrite(TFT_BL, LOW);
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void)
{
  static long tm = millis();
  if (millis() - tm > 300 || LongPress)
  { // don´t allow multiple readings in less than 200ms
    if (touch.touched())
    {
      auto t = touch.getPointScaled();
      t = touch.getPointScaled();
      tm = millis();
      if (rotation == 3)
      {
        t.y = (tftHeight + 20) - t.y;
        t.x = tftWidth - t.x;
      }
      if (rotation == 0)
      {
        int tmp = t.x;
        t.x = tftWidth - t.y;
        t.y = tmp;
      }
      if (rotation == 2)
      {
        int tmp = t.x;
        t.x = t.y;
        t.y = (tftHeight + 20) - tmp;
      }
      // Serial.printf("Touched at x=%d, y=%d, rot=%d\n", t.x, t.y, rotation);

      if (!wakeUpScreen())
        AnyKeyPress = true;
      else
        return;

      // Touch point global variable
      touchPoint.x = t.x;
      touchPoint.y = t.y;
      touchPoint.pressed = true;
      touchHeatMap(touchPoint);
    }
    else
      touchPoint.pressed = false;
  }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {}
