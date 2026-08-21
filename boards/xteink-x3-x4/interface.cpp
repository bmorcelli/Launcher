#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SPI.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <interface.h>
#include <xteink_panel_probe.h>

// Xteink X3 and X4, one binary.
//
// Same board layout, same buttons, same SD card, same display pins — but two
// different panels behind them, and two different ways of reading the battery.
// Which one this is gets settled in _setup_gpio(); see the long comment in
// platformio.ini for the panel index map.
//
// The pin map, the ADC thresholds and the battery curve below all come from
// the open-x4 community SDK (https://github.com/open-x4-epaper/community-sdk),
// which is what the replacement firmwares for this device are built on. There
// is no vendor documentation; those values were read off real hardware.

// Six of the seven buttons hang off two resistor ladders on ADC pins. Only the
// power button gets its own GPIO, because it also has to wake the chip.
#define BTN_LADDER_1 1 // Back, Confirm, Left, Right
#define BTN_LADDER_2 2 // Up, Down
#define PWR_BTN 3      // digital, active LOW

#define BAT_LATCH 13 // MOSFET that keeps the rail alive; must be low to power off

// The X3 carries a TI BQ27220 fuel gauge on its own I2C bus. Note SCL lands on
// GPIO0, which on the X4 is the battery ADC instead — the pin is not shared
// between the two roles, the two devices simply use it differently. That is
// also why the gauge probe has to run before anything reads the ADC, and why an
// X4 hands GPIO0 back afterwards.
#define X3_I2C_SDA 20
#define X3_I2C_SCL 0
#define X3_I2C_FREQ 400000
#define BQ27220_ADDR 0x55
#define BQ27220_SOC_REG 0x2C // StateOfCharge(), percent, 16-bit little endian

#define BAT_ADC 0 // X4 only: battery voltage, behind a 1:2 divider

// Panel indices, matching the GXEPD2_PANEL / _ALTn order in platformio.ini.
enum XteinkPanel : uint8_t {
    PANEL_X4_SSD1677 = 0,
    PANEL_X4_UC8179 = 1,
    PANEL_X4_UC8279 = 2,
    PANEL_X3_UC8253 = 3,
    PANEL_X3_UC8279D = 4,
};

// Seeded as the X4, which is what the build macros describe.
static bool isX3 = false;

// Averaged ADC readings from three real devices:
//
//   Back 3512 | Confirm 2694 | Left 1493 | Right 5      (ladder 1)
//   Up   2242 | Down    5                               (ladder 2)
//
// Each bound below is the midpoint between two neighbours, so the decode
// tolerates part-to-part spread that fixed thresholds would not. A reading
// above ADC_IDLE means nothing is pressed — the ladders idle near full scale.
#define ADC_IDLE 3900
static const int LADDER_1_BOUNDS[] = {ADC_IDLE, 3100, 2090, 750, INT32_MIN};
static const int LADDER_2_BOUNDS[] = {ADC_IDLE, 1120, INT32_MIN};

// Indices into LADDER_1_BOUNDS.
enum { NAV_BACK = 0, NAV_CONFIRM, NAV_LEFT, NAV_RIGHT, NAV_COUNT };
// Indices into LADDER_2_BOUNDS.
enum { PAGE_UP = 0, PAGE_DOWN, PAGE_COUNT };

/***************************************************************************************
** Function name: buttonFromLadder()
** Description:   Which rung of a resistor ladder is shorted, or -1 for none
***************************************************************************************/
static int buttonFromLadder(int value, const int *bounds, int count) {
    for (int i = 0; i < count; ++i) {
        if (bounds[i + 1] < value && value <= bounds[i]) return i;
    }
    return -1;
}

/***************************************************************************************
** Function name: _detect_model()
** Description:   X3 or X4, decided by whether the fuel gauge answers
**
** The X3 has a BQ27220 at 0x55 and the X4 has nothing on that bus at all — it
** uses GPIO0, the X3's SCL, as its battery ADC. So this has to run before
** anything reads the ADC, and an X4 has to get GPIO0 back afterwards.
***************************************************************************************/
static void _detect_model() {
    Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
    // A gauge that does not answer must not stall the UI for the default
    // timeout on every redraw.
    Wire.setTimeOut(4);
    Wire.beginTransmission(BQ27220_ADDR);
    isX3 = Wire.endTransmission() == 0;

    if (!isX3) {
        // Hand GPIO0 back: on an X4 it is the battery divider, not SCL.
        Wire.end();
        return;
    }

    displayConfig.width = 792;
    displayConfig.height = 528;
    device_name = "xteink x3";
    ota_tag = "xteink-x3";
}

/***************************************************************************************
** Function name: _detect_panel()
** Description:   which controller is behind the glass
**
** Runs before SPI.begin(): the probe bit-bangs these pins itself. See
** lib/xteink_panel/xteink_panel_probe.h for how it tells the parts apart, and the
** header of platformio.ini for what each index is.
***************************************************************************************/
static void _detect_panel() {
    const XteinkPanelPins pins = {TFT_CS, TFT_DC, TFT_RST, TFT_BUSY, TFT_SCLK, TFT_MOSI};
    uint8_t ver[5];
    const XteinkSilicon silicon = xteinkProbeSilicon(pins, ver);

    uint8_t panel;
    if (isX3) {
        // The X3 never shipped an SSD1677, so a low BUSY there is not a
        // different controller, it is a panel that did not answer. Either way
        // the original UC8253 is the safe reading.
        panel = (silicon == XTEINK_SILICON_UC8279) ? PANEL_X3_UC8279D : PANEL_X3_UC8253;
    } else {
        switch (silicon) {
            case XTEINK_SILICON_SSD1677: panel = PANEL_X4_SSD1677; break;
            case XTEINK_SILICON_UC8279: panel = PANEL_X4_UC8279; break;
            default: panel = PANEL_X4_UC8179; break;
        }
    }
    displayConfig.driver = panel;

    // Printed on every boot on purpose. Only the SSD1677 test is solid; the
    // UltraChip parts are separated by a VER field this project has not been
    // able to check against real silicon, so when a unit comes up dark these
    // five bytes are what turns the report into a fix.
    launcherConsolePrintf(
        "Panel: %s %s, index %u (VER %02X %02X %02X %02X %02X)\n",
        isX3 ? "X3" : "X4",
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
    launcherGpioInput(BTN_LADDER_1);
    launcherGpioInput(BTN_LADDER_2);
    launcherGpioInputPullup(PWR_BTN);
    // The ladders swing across the whole rail, so the ADC needs its widest range.
    analogSetAttenuation(ADC_11db);

    // Panel and SD card share one bus: park both chip selects high before it
    // comes up, so neither answers while the other is being addressed.
    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(SDCARD_CS);
    launcherGpioWrite(SDCARD_CS, HIGH);

    _detect_model();
    _detect_panel(); // bit-bangs the display pins, so before SPI takes them

    // Started here rather than by the GxEPD2 backend: the panel is write-only
    // and would leave the bus without a MISO for the card.
    SPI.begin(TFT_SCLK, SDCARD_MISO, TFT_MOSI, TFT_CS);
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
    if (isX3) {
        // The gauge already reports a state of charge, so there is no curve to
        // fit. It is also slow to answer over a 400kHz bus, and the launcher
        // asks on every header redraw, so cache it — and on an I2C error keep
        // the last reading rather than blinking to zero.
        static int cached = 0;
        static unsigned long lastPoll = 0;
        const unsigned long now = launcherMillis();
        if (lastPoll != 0 && (now - lastPoll) < 1500) return cached;
        lastPoll = now;

        Wire.beginTransmission(BQ27220_ADDR);
        Wire.write(BQ27220_SOC_REG);
        if (Wire.endTransmission(false) != 0) return cached;
        if (Wire.requestFrom(BQ27220_ADDR, 2) < 2) return cached;

        const uint8_t lo = Wire.read();
        const uint8_t hi = Wire.read();
        const uint16_t soc = ((uint16_t)hi << 8) | lo;
        cached = soc > 100 ? 100 : (int)soc;
        return cached;
    }

    const uint32_t mv = analogReadMilliVolts(BAT_ADC) * 2; // 1:2 divider
    const double volts = mv / 1000.0;

    // Cubic fit over LiPo discharge samples taken from this device. It beats a
    // straight voltage-to-percent ramp badly in the flat middle of the curve,
    // which is where a reader spends most of its life.
    double percent =
        -144.9390 * volts * volts * volts + 1655.8629 * volts * volts - 6158.8520 * volts + 7501.3202;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return (int)(percent + 0.5);
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    // No backlight and no frontlight on this panel.
    (void)brightval;
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = launcherMillis();
    if (launcherMillis() - tm > 200 || LongPress) {
    } else return;

    const int nav = buttonFromLadder(analogRead(BTN_LADDER_1), LADDER_1_BOUNDS, NAV_COUNT);
    const int page = buttonFromLadder(analogRead(BTN_LADDER_2), LADDER_2_BOUNDS, PAGE_COUNT);
    const bool power = launcherGpioRead(PWR_BTN) == LOW;

    if (nav < 0 && page < 0 && !power) return;

    tm = launcherMillis();
    if (!wakeUpScreen()) AnyKeyPress = true;
    else return;

    switch (nav) {
        case NAV_BACK: EscPress = true; break;
        case NAV_CONFIRM: SelPress = true; break;
        case NAV_LEFT: PrevPress = true; break;
        case NAV_RIGHT: NextPress = true; break;
        default: break;
    }

    if (page == PAGE_UP) UpPress = true;
    else if (page == PAGE_DOWN) DownPress = true;
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    // Letting go first, or the wake-up source is already asserted when we arm it.
    while (launcherGpioRead(PWR_BTN) == LOW) launcherDelayMs(50);
    launcherDelayMs(100);

    // GPIO13 drives the battery latch MOSFET. It has to be driven low *and*
    // held there through sleep, otherwise the rail comes straight back up.
    gpio_set_direction((gpio_num_t)BAT_LATCH, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)BAT_LATCH, 0);
    gpio_hold_en((gpio_num_t)BAT_LATCH);
    gpio_deep_sleep_hold_en();

    // The C3 has no ext0/ext1 wake sources; GPIO wake-up is the one it offers.
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PWR_BTN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}
