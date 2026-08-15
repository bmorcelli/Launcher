#include "cardkb2.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedKEYBOARD.h>
#include <Wire.h>
#include <interface.h>

/***************************************************************************************
** Tab5 built-in keyboard (M5Unit-KEYBOARD) integration
**
** The keyboard hangs off ExtPort1 (internal 10-pin connector):
**   INT = GPIO50 (J9 pin 10)   SDA = GPIO0 (J9 pin 7)   SCL = GPIO1 (J9 pin 8)
**
** It is started from _setup_gpio(). When the keyboard is absent at boot we arm a
** falling-edge interrupt on the INT pin (GPIO50) so a keyboard connected/used later
** flags a hot-plug; InputHandler() then retries the initialization and, once the
** device answers, the navigation flow is enabled.
***************************************************************************************/
namespace kb = m5::unit::tab5_keyboard;

static constexpr int8_t TAB5_KB_SDA = 0;
static constexpr int8_t TAB5_KB_SCL = 1;
static constexpr gpio_num_t TAB5_KB_INT = GPIO_NUM_50;
static constexpr uint8_t TAB5_HID_ENTER = 0x28;
static constexpr uint8_t TAB5_HID_ESC = 0x29;
static constexpr uint8_t TAB5_HID_BACKSPACE = 0x2A;
static constexpr uint8_t TAB5_HID_TAB = 0x2B;
static constexpr uint8_t TAB5_HID_DELETE = 0x4C;
static constexpr uint8_t TAB5_HID_RIGHT = 0x4F;
static constexpr uint8_t TAB5_HID_LEFT = 0x50;
static constexpr uint8_t TAB5_HID_DOWN = 0x51;
static constexpr uint8_t TAB5_HID_UP = 0x52;
static constexpr uint8_t TAB5_HID_MOD_LCTRL = 0x01;
static constexpr uint8_t TAB5_HID_MOD_LSHIFT = 0x02;
static constexpr uint8_t TAB5_HID_MOD_LALT = 0x04;
static constexpr uint8_t TAB5_KEY_LEFT_CTRL = 0x80;
static constexpr uint8_t TAB5_KEY_LEFT_SHIFT = 0x81;
static constexpr uint8_t TAB5_KEY_LEFT_ALT = 0x82;

static m5::unit::UnitUnified tab5KbUnits;
static m5::unit::UnitTab5Keyboard tab5Kb;
static bool tab5KbAdded = false;            // Units.add() must run exactly once
static bool tab5KbReady = false;            // true once begin() succeeds
static volatile bool tab5KbHotplug = false; // set by the INT ISR when kb absent

// Minimal ISR: just latch the hot-plug request; the real work happens in InputHandler().
static void IRAM_ATTR tab5KbHotplugIsr() { tab5KbHotplug = true; }

// Watch the INT line so a keyboard plugged in / used later can be detected.
static void tab5KbArmHotplug() {
    pinMode(TAB5_KB_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TAB5_KB_INT), tab5KbHotplugIsr, FALLING);
}

// Try to bring the keyboard up. Returns true when the device answers on I2C.
// The library installs its own GPIO50 ISR (event draining) inside begin() once the
// device is confirmed, so begin() failing leaves the INT pin free for our hot-plug ISR.
static bool tab5KbBegin() {
    if (!tab5KbAdded) {
        auto cfg = tab5Kb.config();
        cfg.mode = kb::Mode::Normal; // Normal mode exposes the bitwise per-key state
        cfg.irq_pin = TAB5_KB_INT;   // library drains events on this INT
        tab5Kb.config(cfg);

        Wire.end(); // Closes Wire instance opened by CardKb
        Wire.begin(TAB5_KB_SDA, TAB5_KB_SCL, tab5Kb.component_config().clock);
        if (!tab5KbUnits.add(tab5Kb, Wire)) return false;
        tab5KbAdded = true;
    }
    return tab5KbUnits.begin();
}

// Called from _setup_gpio(): first init attempt. On failure, arm the hot-plug INT.
static void tab5KbSetup() {
    tab5KbReady = tab5KbBegin();
    if (tab5KbReady) {
        launcherConsolePrintf("Tab5 keyboard ready (fw 0x%02X)\n", tab5Kb.firmwareVersion());
    } else {
        launcherConsolePrintf("Tab5 keyboard absent, arming hot-plug INT on GPIO%d\n", (int)TAB5_KB_INT);
        tab5KbArmHotplug();
    }
}

// Called from InputHandler() once the hot-plug ISR fired: retry the init.
static void tab5KbRetry() {
    // Release our hot-plug ISR so begin() can install the library's own GPIO50 handler.
    detachInterrupt(digitalPinToInterrupt(TAB5_KB_INT));
    tab5KbReady = tab5KbBegin();
    if (tab5KbReady) {
        launcherConsolePrintf("Tab5 keyboard connected (fw 0x%02X)\n", tab5Kb.firmwareVersion());
    } else {
        // Still nothing there: keep listening for the next hot-plug edge.
        tab5KbArmHotplug();
    }
}

// Drain the keyboard and translate key presses into the navigation globals.
static void tab5KbPoll() {
    if (!tab5KbReady) return;
    tab5KbUnits.update();
    if (!tab5Kb.wasPressed()) return;

    // A key pressed while the screen sleeps only wakes it up (no navigation).
    if (wakeUpScreen()) {
        AnyKeyPress = true;
        return;
    }

    keyStroke pendingKey;
    bool keyPulse = false;
    const bool sym = tab5Kb.isSym();
    const bool aa = tab5Kb.isAa();
    const bool ctrl = tab5Kb.isCtrl();
    const bool alt = tab5Kb.isAlt();

    for (uint8_t kidx = 0; kidx < kb::KEY_COUNT; ++kidx) {
        if (!tab5Kb.wasPressed(kidx)) continue;
        const uint8_t row = static_cast<uint8_t>(kidx / kb::KEY_COL_COUNT);
        const uint8_t col = static_cast<uint8_t>(kidx % kb::KEY_COL_COUNT);
        const kb::HidMapping map = sym ? kb::keyMatrixToHidSym(row, col) : kb::keyMatrixToHidBase(row, col);

        if (map.keycode != 0) {
            pendingKey.hid_keys.emplace_back(map.keycode);
            pendingKey.modifiers |= map.modifier;
            const char ch = tab5Kb.keyMatrixToChar(kidx);
            if (ch != 0 && ch != '\n' && ch != '\b' && ch != '\t') pendingKey.word.emplace_back(ch);
        }

        switch (map.keycode) {
            case TAB5_HID_LEFT: PrevPress = true; break;
            case TAB5_HID_RIGHT: NextPress = true; break;
            case TAB5_HID_UP: UpPress = true; break;
            case TAB5_HID_DOWN: DownPress = true; break;
            case TAB5_HID_ENTER:
                pendingKey.enter = true;
                SelPress = true;
                break;
            case TAB5_HID_ESC:
                pendingKey.exit_key = true;
                pendingKey.fn = true; // Existing text UI cancels on fn+exit_key.
                EscPress = true;
                break;
            case TAB5_HID_BACKSPACE:
            case TAB5_HID_DELETE: pendingKey.del = true; break;
            case TAB5_HID_TAB: pendingKey.word.emplace_back('\t'); break;
            default: break;
        }
        keyPulse = true;
        AnyKeyPress = true;
    }

    if (keyPulse) {
        if (aa) {
            pendingKey.modifiers |= TAB5_HID_MOD_LSHIFT;
            pendingKey.modifier_keys.emplace_back(TAB5_KEY_LEFT_SHIFT);
        }
        if (ctrl) {
            pendingKey.modifiers |= TAB5_HID_MOD_LCTRL;
            pendingKey.modifier_keys.emplace_back(TAB5_KEY_LEFT_CTRL);
        }
        if (alt) {
            pendingKey.modifiers |= TAB5_HID_MOD_LALT;
            pendingKey.modifier_keys.emplace_back(TAB5_KEY_LEFT_ALT);
        }
        if (sym) pendingKey.fn = true;
        pendingKey.pressed = true;
        KeyStroke = pendingKey;
    } else {
        KeyStroke.Clear();
    }
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    /*
    Can't be set in build_flags
    -D FP=3
    -D FM=6
    -D FG=9
    */
    _fp = 3;
    _fm = 6;
    _fg = 9;

    M5.begin();
    M5.Power.setExtOutput(true);
    launcherWifiInitHostedSdioGuarded(
        SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST
    );
}

void _late_setup_gpio() {
    // Try to brig up tab5 keyboard after CardKB
    // Need time to bring up the keyboard
    if (!CardKB2Installed) tab5KbSetup();
}
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
    // Late keyboard hot-plug: the INT ISR flagged activity while the keyboard was
    // absent, so retry the initialization and enable the navigation flow.
    if (!tab5KbReady && tab5KbHotplug) {
        tab5KbHotplug = false;
        tab5KbRetry();
    }
    tab5KbPoll();

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
