#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <esp_sleep.h>
#include <interface.h>
#include <xteink_panel_probe.h>

// Xteink X4 Pro — ESP32-S3, 800x480 e-paper, GT911 touch, dual frontlight.
//
// Every pin and every sequence below is transcribed from the FreeInk SDK's
// bring-up document, which marks them confirmed on hardware:
//   https://github.com/Free-Ink/freeink-sdk/blob/main/docs/xteink-x4pro-support.md
// Where that document flags a detail as non-obvious, the reason is repeated
// here — several of them silently produce a dead peripheral rather than an
// error, and cost the original authors a bring-up session each.
//
// Not tested on hardware here.

// --- buttons ---------------------------------------------------------------
// Plain digital, active low. GPIO0 is a boot-strap pin; it works as a button
// as long as it is not held through reset.
#define BTN_LEFT 0
#define BTN_RIGHT 7
#define BTN_POWER 3

// --- power rails -----------------------------------------------------------
#define RAIL_PERIPH 1 // OUTPUT HIGH; the GT911 does not come up without it
#define RAIL_TOUCH 2  // OUTPUT LOW — active LOW. High or floating = dark GT911
#define RAIL_SD 5     // OUTPUT LOW — active LOW, and must STAY low after mount

// --- I2C bus: RTC (0x51), fuel gauge (0x63), touch (0x5D) ------------------
#define I2C_SDA 39
#define I2C_SCL 38
#define I2C_FREQ 400000

// --- GT911 touch -----------------------------------------------------------
#define GT911_ADDR 0x5D
#define GT911_INT 10
#define GT911_RST 4
#define GT911_REG_STATUS 0x814E
#define GT911_REG_POINT1 0x8150
#define GT911_STATUS_READY 0x80
#define GT911_STATUS_HOME 0x10 // capacitive Home pad, not a GPIO

// --- CW2017 fuel gauge -----------------------------------------------------
#define CW2017_ADDR 0x63
#define CW2017_REG_VERSION 0x00
#define CW2017_REG_SOC 0x04
#define CW2017_REG_MODE 0x08
#define CW2017_REG_CONFIG 0x0B
#define CW2017_REG_BATINFO 0x10

// --- frontlight ------------------------------------------------------------
#define FL_COOL 8 // white channel
#define FL_WARM 9 // warm channel
#define FL_FREQ 10000
#define FL_BITS 10
#define FL_MAX ((1 << FL_BITS) - 1)

// The gauge reports 0% until a battery profile matching this cell is resident,
// so it has to be uploaded before any reading means anything. Table recovered
// from the OEM firmware; it is specific to the X4 Pro's cell.
// clang-format off
static const uint8_t CW2017_BATINFO[80] = {
    0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaa, 0xbf, 0xb5, 0xb4, 0xa4, 0x9c, 0xeb, 0xe2,
    0xdf, 0xe5, 0xca, 0xa0, 0x8a, 0x62, 0x53, 0x48, 0x40, 0x3a, 0x32, 0xb1, 0xae, 0xda, 0xb5, 0xff,
    0xff, 0xff, 0xe8, 0xdb, 0xd9, 0xd6, 0xd4, 0xd2, 0xd0, 0xcb, 0xc3, 0xbc, 0x9e, 0x87, 0x7b, 0x71,
    0x72, 0x7c, 0x8c, 0xa3, 0xb7, 0xc8, 0xa5, 0x4f, 0x00, 0x00, 0xab, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23
};
// clang-format on
static bool touchReady = false;

/*********************************************************************
** 8-bit register helpers (fuel gauge, RTC)
**********************************************************************/
static bool i2cReadReg8(uint8_t addr, uint8_t reg, uint8_t &out) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)1) < 1) return false;
    out = Wire.read();
    return true;
}

static bool i2cWriteReg8(uint8_t addr, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

/*********************************************************************
** GT911 uses 16-bit register addresses, big endian
**********************************************************************/
static bool gt911Read(uint16_t reg, uint8_t *buf, size_t len) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)GT911_ADDR, (uint8_t)len) < (int)len) return false;
    for (size_t i = 0; i < len; ++i) buf[i] = Wire.read();
    return true;
}

static bool gt911WriteStatus(uint8_t value) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write((uint8_t)(GT911_REG_STATUS >> 8));
    Wire.write((uint8_t)(GT911_REG_STATUS & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

/*********************************************************************
** Function: gt911Begin
** The controller loads its own config from internal storage during the reset
** dance — there is no config table to upload, in the OEM firmware or here. But
** the load only triggers on a long enough reset: cut it short and the config
** registers read back zero and the panel never scans, with no error anywhere.
**********************************************************************/
static bool gt911Begin() {
    launcherGpioOutput(GT911_RST);
    launcherGpioOutput(GT911_INT);

    // INT is the address-select line: held low as RST rises picks 0x5D.
    launcherGpioWrite(GT911_RST, LOW);
    launcherGpioWrite(GT911_INT, LOW);
    launcherDelayMs(10);
    launcherGpioWrite(GT911_RST, HIGH);
    launcherDelayMs(10);
    launcherDelayMs(50);
    pinMode(GT911_INT, INPUT);
    launcherDelayMs(50);

    Wire.beginTransmission(GT911_ADDR);
    return Wire.endTransmission() == 0;
}

/*********************************************************************
** Function: cw2017EnsureProfile
** Verify-then-upload. The profile survives warm boots, so this is normally a
** read-only check.
**********************************************************************/
static void cw2017Reset() {
    i2cWriteReg8(CW2017_ADDR, CW2017_REG_MODE, 0xF0);
    launcherDelayMs(20);
    i2cWriteReg8(CW2017_ADDR, CW2017_REG_MODE, 0x30);
    launcherDelayMs(20);
    i2cWriteReg8(CW2017_ADDR, CW2017_REG_MODE, 0x00);
    launcherDelayMs(20);
}

static void cw2017EnsureProfile() {
    uint8_t ver = 0;
    if (!i2cReadReg8(CW2017_ADDR, CW2017_REG_VERSION, ver)) return; // no gauge, nothing to do
    if ((ver & 0xFD) != 0x0D) cw2017Reset();

    uint8_t cfg = 0;
    if (i2cReadReg8(CW2017_ADDR, CW2017_REG_CONFIG, cfg) && (cfg & 0x80)) {
        bool match = true;
        for (uint8_t i = 0; i < sizeof(CW2017_BATINFO); ++i) {
            uint8_t b = 0;
            if (!i2cReadReg8(CW2017_ADDR, (uint8_t)(CW2017_REG_BATINFO + i), b) || b != CW2017_BATINFO[i]) {
                match = false;
                break;
            }
        }
        if (match) return;
    }

    for (uint8_t i = 0; i < sizeof(CW2017_BATINFO); ++i) {
        i2cWriteReg8(CW2017_ADDR, (uint8_t)(CW2017_REG_BATINFO + i), CW2017_BATINFO[i]);
    }
    i2cWriteReg8(CW2017_ADDR, CW2017_REG_CONFIG, 0x80); // update-enable
    launcherDelayMs(20);
    cw2017Reset();

    // Bounded, so a cold gauge cannot stall boot.
    for (int i = 0; i < 50; ++i) {
        uint8_t soc = 0;
        if (i2cReadReg8(CW2017_ADDR, CW2017_REG_SOC, soc) && soc <= 100) break;
        launcherDelayMs(20);
    }
}

/***************************************************************************************
** Function name: _detect_panel()
** Description:   which controller is behind the glass
**
** The 800x480 glass is the same across batches, the silicon behind it is not.
** Index order matches GXEPD2_PANEL / _ALTn in platformio.ini; the probe itself
** is described in lib/xteink_panel/xteink_panel_probe.h. Runs before tft->begin(),
** which is what brings the SPI bus up on this board (GXEPD2_BEGIN_SPI).
***************************************************************************************/
enum X4ProPanel : uint8_t { PANEL_SSD1677 = 0, PANEL_UC8179 = 1, PANEL_UC8279 = 2 };

static void _detect_panel() {
    const XteinkPanelPins pins = {TFT_CS, TFT_DC, TFT_RST, TFT_BUSY, TFT_SCLK, TFT_MOSI};
    uint8_t ver[5];
    const XteinkSilicon silicon = xteinkProbeSilicon(pins, ver);

    uint8_t panel;
    switch (silicon) {
        case XTEINK_SILICON_SSD1677: panel = PANEL_SSD1677; break;
        case XTEINK_SILICON_UC8279: panel = PANEL_UC8279; break;
        default: panel = PANEL_UC8179; break;
    }
    displayConfig.driver = panel;

    // Printed on every boot on purpose: only the SSD1677 test is solid, so when
    // a unit comes up dark these five bytes are what turns the report into a fix.
    launcherConsolePrintf(
        "Panel: %s, index %u (VER %02X %02X %02X %02X %02X)\n",
        silicon == XTEINK_SILICON_SSD1677  ? "SSD1677"
        : silicon == XTEINK_SILICON_UC8279 ? "UC8279"
                                           : "UltraChip",
        panel,
        ver[0],
        ver[1],
        ver[2],
        ver[3],
        ver[4]
    );
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioInputPullup(BTN_LEFT);
    launcherGpioInputPullup(BTN_RIGHT);
    launcherGpioInputPullup(BTN_POWER);

    // Order matters: the touch rail needs the peripheral rail already high.
    launcherGpioOutput(RAIL_PERIPH);
    launcherGpioWrite(RAIL_PERIPH, HIGH);
    launcherGpioOutput(RAIL_TOUCH);
    launcherGpioWrite(RAIL_TOUCH, LOW); // active low
    launcherGpioOutput(RAIL_SD);
    launcherGpioWrite(RAIL_SD, LOW); // active low

    Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
    launcherDelayMs(10);
    cw2017EnsureProfile();

    _detect_panel();
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup, run after TFT and before SD card init
***************************************************************************************/
void _post_setup_gpio() {
    touchReady = gt911Begin();

    ledcAttach(FL_COOL, FL_FREQ, FL_BITS);
    ledcAttach(FL_WARM, FL_FREQ, FL_BITS);
    _setBrightness((uint8_t)bright);

    // Power-cycle the card's data path before the mount that follows. Without
    // this the first block read lands on a still-marginal path and fails; the
    // rail then has to be left LOW, because driving it high after init breaks
    // every subsequent read.
    launcherGpioWrite(RAIL_SD, HIGH);
    launcherDelayMs(80);
    launcherGpioWrite(RAIL_SD, LOW);
    launcherDelayMs(120);
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    // The launcher asks on every header redraw and the gauge is on a 400 kHz
    // bus, so cache; on an I2C error keep the last value rather than blink to 0.
    static int cached = 0;
    static unsigned long lastPoll = 0;
    const unsigned long now = launcherMillis();
    if (lastPoll != 0 && (now - lastPoll) < 1500) return cached;
    lastPoll = now;

    uint8_t soc = 0;
    if (!i2cReadReg8(CW2017_ADDR, CW2017_REG_SOC, soc)) return cached;
    if (soc > 100) soc = 100;
    cached = soc;
    return cached;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    // The panel has a warm channel and a cool one. The launcher has no notion
    // of colour temperature, so both are driven together for a neutral mix —
    // the split is what the setting would control if it existed.
    const uint32_t duty = ((uint32_t)brightval * FL_MAX) / 100;
    ledcWrite(FL_COOL, duty);
    ledcWrite(FL_WARM, duty);
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = launcherMillis();
    if (launcherMillis() - tm > 200 || LongPress) {
    } else return;

    const bool left = launcherGpioRead(BTN_LEFT) == LOW;
    const bool right = launcherGpioRead(BTN_RIGHT) == LOW;

    uint8_t status = 0;
    uint8_t home = 0;
    uint8_t points = 0;
    uint16_t tx = 0, ty = 0;

    if (touchReady && gt911Read(GT911_REG_STATUS, &status, 1) && (status & GT911_STATUS_READY)) {
        home = status & GT911_STATUS_HOME;
        points = status & 0x0F;
        if (points > 0) {
            uint8_t p[8] = {0};
            if (gt911Read(GT911_REG_POINT1, p, sizeof(p))) {
                // X low byte first on this controller.
                const uint16_t rawX = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
                const uint16_t rawY = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
                // The glass is mounted portrait: it reports X 0-480 / Y 0-800
                // against an 800x480 landscape panel, so the axes swap.
                tx = rawY;
                ty = rawX;
            } else {
                points = 0;
            }
        }
        gt911WriteStatus(0); // must be cleared or the controller stops reporting
    }

    if (!left && !right && !home && points == 0) return;

    tm = launcherMillis();
    if (!wakeUpScreen()) AnyKeyPress = true;
    else return;

    if (left) PrevPress = true;
    if (right) NextPress = true;
    if (home) EscPress = true;

    if (points > 0) {
        touchPoint.x = tx;
        touchPoint.y = ty;
        touchPoint.pressed = true;
        touchHeatMap(touchPoint);
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    while (launcherGpioRead(BTN_POWER) == LOW) launcherDelayMs(50);
    launcherDelayMs(100);

    _setBrightness(0);

    // Both enables are active low, so their off state is high.
    launcherGpioWrite(RAIL_TOUCH, HIGH);
    launcherGpioWrite(RAIL_SD, HIGH);

    // Unlike the C3 models there is no battery latch to hold here — that GPIO13
    // trick belongs to the X3/X4 board and does nothing on this one.
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_POWER, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {
    if (launcherGpioRead(BTN_POWER) != LOW) return;

    const uint32_t start = launcherMillis();
    int lastCountDown = -1;
    while (launcherGpioRead(BTN_POWER) == LOW) {
        if (launcherMillis() - start > 500) {
            const int countDown = (launcherMillis() - start) / 1000 + 1;
            if (countDown < 3) {
                // One refresh per second at most: this panel cannot repaint
                // per frame.
                if (countDown != lastCountDown) {
                    lastCountDown = countDown;
                    tft->setTextSize(1);
                    tft->setTextColor(FGCOLOR, BGCOLOR);
                    tft->drawCentreString("PWR OFF IN " + String(countDown) + "/2", tftWidth / 2, 12, 1);
                    tft->display();
                }
            } else {
                tft->fillScreen(BGCOLOR);
                tft->display();
                powerOff();
            }
        }
        launcherDelayMs(10);
    }

    if (lastCountDown >= 0) {
        tft->fillRect(0, 12, tftWidth, LH, BGCOLOR);
        tft->display();
    }
}
