#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>
#ifdef USE_CARDKB2
#include <cardkb2.h>
#endif

constexpr uint32_t kBtnBDoublePressWindowMs = 270;
constexpr uint32_t kBtnBLongPressMs = 500;
constexpr uint32_t kBtnDebounceMs = 8;

#define BTN_A_PIN 11
#define BTN_B_PIN 12

// --- M5PM1 power-management IC ------------------------------------------
#define PM1_SDA 47
#define PM1_SCL 48
#define PM1_I2C_ADDR 0x6E
#define PM1_REG_PWR_CFG 0x06
#define PM1_REG_I2C_CFG 0x09
#define PM1_REG_WDT_CNT 0x0A
#define PM1_REG_SYS_CMD 0x0C
#define PM1_REG_VBAT_L 0x22
#define PM1_PWR_CFG_BOOST_EN (1 << 3)
#define PM1_SYS_CMD_SHUTDOWN 0xA1

static bool pm1WriteReg8(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(PM1_I2C_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    return Wire1.endTransmission() == 0;
}

static bool pm1ReadReg(uint8_t reg, uint8_t *buf, size_t len) {
    Wire1.beginTransmission(PM1_I2C_ADDR);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom((int)PM1_I2C_ADDR, (int)len) != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire1.read();
    return true;
}

static void pm1SetExtOutput(bool enable) {
    uint8_t cfg = 0;
    pm1ReadReg(PM1_REG_PWR_CFG, &cfg, 1);
    if (enable) cfg |= PM1_PWR_CFG_BOOST_EN;
    else cfg &= ~PM1_PWR_CFG_BOOST_EN;
    pm1WriteReg8(PM1_REG_PWR_CFG, cfg);
}

// --- Button debounce ------------------------------------------------------
// Stands in for M5Unified's Button_Class: same 8ms debounce, same hold/click
// semantics InputHandler() below relies on.
struct DebouncedButton {
    uint8_t pin;
    bool raw = false;
    bool stable = false;
    uint32_t lastChangeMs = 0;
    uint32_t pressStartMs = 0;
    bool wasPressedEdge = false;
    bool wasReleasedEdge = false;

    void update(uint32_t now) {
        bool r = (launcherGpioRead(pin) == LOW);
        if (r != raw) {
            raw = r;
            lastChangeMs = now;
        }
        wasPressedEdge = wasReleasedEdge = false;
        if (stable != raw && (now - lastChangeMs) >= kBtnDebounceMs) {
            stable = raw;
            if (stable) {
                pressStartMs = now;
                wasPressedEdge = true;
            } else {
                wasReleasedEdge = true;
            }
        }
    }

    bool pressedFor(uint32_t ms) const { return stable && (launcherMillis() - pressStartMs) >= ms; }
};

static DebouncedButton btnA{BTN_A_PIN};
static DebouncedButton btnB{BTN_B_PIN};

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    Wire1.begin(PM1_SDA, PM1_SCL);
    pm1WriteReg8(PM1_REG_I2C_CFG, 0x00); // disable I2C idle sleep
    pm1WriteReg8(PM1_REG_WDT_CNT, 0x00); // disable watchdog

#ifndef USE_CARDKB2
    // Disable 5V output to external port. With CardKB2 support the rail must
    // stay on from boot so the keyboard's MCU is booted by probe time;
    // _post_setup_gpio() turns it off when no keyboard is found.
    pm1SetExtOutput(false);
#else
    pm1SetExtOutput(true); // CardKB2 needs Grove 5V, energized directly at boot
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

    launcherGpioInput(BTN_A_PIN);
    launcherGpioInput(BTN_B_PIN);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);

#ifdef USE_CARDKB2
    // CardKB2 on the Grove port (G9/G10). Probing reconfigures G9 as I2C SDA,
    // so restore the RF433 anti-jam state if no keyboard is attached.
    if (!CardKB2Installed) {
        pm1SetExtOutput(false);
        launcherGpioOutput(9);
        launcherGpioWrite(9, LOW); // M5RF433 avoid Jamming
    }
#endif
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle = (brightval * 255) / 100;
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    uint8_t buf[2] = {0, 0};
    if (!pm1ReadReg(PM1_REG_VBAT_L, buf, sizeof(buf))) return 0;
    float mv = (float)((buf[1] << 8) | buf[0]);
    int level = (int)((mv - 3300.0f) * 100.0f / (4150.0f - 3350.0f));
    return (level < 0) ? 0 : (level >= 100) ? 100 : level;
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static uint32_t btnBFirstReleaseMs = 0;
    static bool btnBWaitingSecondClick = false;
    static bool btnBLongPressFired = false;

    uint32_t now = launcherMillis();
    btnA.update(now);
    btnB.update(now);

    bool emitNext = false;
    bool emitPrev = false;
    bool emitEsc = false;
    bool btnAActive = btnA.stable;
    bool btnBActive = btnB.stable;

    if (btnB.wasPressedEdge) btnBLongPressFired = false;

    if (btnBActive && !btnBLongPressFired && btnB.pressedFor(kBtnBLongPressMs)) {
        btnBLongPressFired = true;
        btnBWaitingSecondClick = false;
        emitEsc = true;
    }

    if (btnB.wasReleasedEdge) {
        if (btnBLongPressFired) {
            btnBLongPressFired = false;
        } else if (btnBWaitingSecondClick && now - btnBFirstReleaseMs <= kBtnBDoublePressWindowMs) {
            btnBWaitingSecondClick = false;
            emitPrev = true;
        } else {
            btnBWaitingSecondClick = true;
            btnBFirstReleaseMs = now;
        }
    }

    if (btnBWaitingSecondClick && !btnBActive && now - btnBFirstReleaseMs > kBtnBDoublePressWindowMs) {
        btnBWaitingSecondClick = false;
        emitNext = true;
    }

    bool btnAClicked = btnA.wasReleasedEdge;

    if (btnAActive || btnBActive || btnBWaitingSecondClick || btnAClicked || emitNext || emitPrev || emitEsc)
        AnyKeyPress = true;
    if (!AnyKeyPress) return;

    if ((btnAActive || btnBActive) && wakeUpScreen()) return;

    if (btnAClicked) SelPress = true;
    if (emitNext) NextPress = true;
    if (emitPrev) PrevPress = true;
    if (emitEsc) EscPress = true;
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { pm1WriteReg8(PM1_REG_SYS_CMD, PM1_SYS_CMD_SHUTDOWN); }
