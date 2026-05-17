#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5Unified.h>
#include <interface.h>

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {

    M5.begin();
    launcherWifiInitHostedSdio(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int percent;
    percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) { M5.Display.setBrightness(brightval); }

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long tm = launcherMillis();
    if (launcherMillis() - tm > 200 || LongPress) {
        M5.update();
        auto t = M5.Touch.getDetail();
        if (t.isPressed() || t.isHolding()) {
            // launcherConsolePrintf("x1=%d, y1=%d, ", t.x, t.y);
            tm = launcherMillis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;
            // launcherConsolePrintf("x2=%d, y2=%d, rot=%d\n", t.x, t.y, rotation);

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        } else touchPoint.pressed = false;
    }
}
/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { M5.Power.powerOff(); }

/*********************************************************************
** Function: reboot
** location: mykeyboard.cpp
** Reboots the device
**********************************************************************/
static bool tab5_is_leap_year_full(int year_full) {
    if (year_full % 400 == 0) return true;
    if (year_full % 100 == 0) return false;
    return (year_full % 4 == 0);
}

static int tab5_days_in_month_full(int year_full, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) return tab5_is_leap_year_full(year_full) ? 29 : 28;
    if (month >= 1 && month <= 12) return days[month - 1];
    return 30;
}

static void tab5_adjust_date_minutes(int &year, int &month, int &day, int &hour, int &minute, int delta_min) {
    int total = hour * 60 + minute + delta_min;
    if (total >= 0 && total < 1440) {
        hour = total / 60;
        minute = total % 60;
        return;
    }
    if (total < 0) {
        total += 1440;
        hour = total / 60;
        minute = total % 60;
        day -= 1;
        if (day < 1) {
            month -= 1;
            if (month < 1) {
                month = 12;
                year -= 1;
            }
            day = tab5_days_in_month_full(year, month);
        }
    } else {
        total -= 1440;
        hour = total / 60;
        minute = total % 60;
        day += 1;
        int dim = tab5_days_in_month_full(year, month);
        if (day > dim) {
            day = 1;
            month += 1;
            if (month > 12) {
                month = 1;
                year += 1;
            }
        }
    }
}

static bool tab5_is_leap_year(uint16_t year_full) {
    if (year_full % 400 == 0) return true;
    if (year_full % 100 == 0) return false;
    return (year_full % 4 == 0);
}

void reboot() {
    auto &ioe = M5.getIOExpander(1);
    if (M5.Rtc.isEnabled()) {
        launcherConsolePrintf("%s\n", String("reboot: RTC alarm").c_str());
        M5.Rtc.clearIRQ();
        auto now = M5.Rtc.getDateTime();

        auto set_dt = now;
        auto alarm_dt = now;

        if (now.time.seconds <= 29) {
            // set to hh:mm-1:59 and alarm hh:mm:00
            set_dt.time.seconds = 59;
            int y = set_dt.date.year;
            int mo = set_dt.date.month;
            int d = set_dt.date.date;
            int h = set_dt.time.hours;
            int mi = set_dt.time.minutes;
            tab5_adjust_date_minutes(y, mo, d, h, mi, -1);
            set_dt.date.year = y;
            set_dt.date.month = mo;
            set_dt.date.date = d;
            set_dt.time.hours = h;
            set_dt.time.minutes = mi;

            alarm_dt.time.seconds = 0;
        } else {
            // set to hh:mm:59 and alarm hh:mm+1:00
            set_dt.time.seconds = 59;

            alarm_dt.time.seconds = 0;
            int y = alarm_dt.date.year;
            int mo = alarm_dt.date.month;
            int d = alarm_dt.date.date;
            int h = alarm_dt.time.hours;
            int mi = alarm_dt.time.minutes;
            tab5_adjust_date_minutes(y, mo, d, h, mi, 1);
            alarm_dt.date.year = y;
            alarm_dt.date.month = mo;
            alarm_dt.date.date = d;
            alarm_dt.time.hours = h;
            alarm_dt.time.minutes = mi;
        }

        M5.Rtc.setDateTime(set_dt);
        M5.Rtc.setAlarmIRQ(alarm_dt.date, alarm_dt.time);
    }
    for (int i = 0; i < 3; ++i) {
        ioe.digitalWrite(4, HIGH);
        launcherDelayMs(100);
        ioe.digitalWrite(4, LOW);
        launcherDelayMs(100);
    }
}
