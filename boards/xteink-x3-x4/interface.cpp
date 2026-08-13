#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SPI.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <interface.h>

// Xteink X3 and X4.
//
// Same board layout, same buttons, same SD card, same display pins — but two
// different panels behind them, and two different ways of reading the battery.
// DISPLAY_XTEINK_X3 selects the X3.
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

#if defined(DISPLAY_XTEINK_X3)
// The X3 carries a TI BQ27220 fuel gauge on its own I2C bus. Note SCL lands on
// GPIO0, which on the X4 is the battery ADC instead — the pin is not shared
// between the two roles, the two devices simply use it differently.
#define X3_I2C_SDA 20
#define X3_I2C_SCL 0
#define X3_I2C_FREQ 400000
#define BQ27220_ADDR 0x55
#define BQ27220_SOC_REG 0x2C // StateOfCharge(), percent, 16-bit little endian
#else
#define BAT_ADC 0 // battery voltage, behind a 1:2 divider
#endif

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

    // Started here rather than by the GxEPD2 backend: the panel is write-only
    // and would leave the bus without a MISO for the card.
    SPI.begin(TFT_SCLK, SDCARD_MISO, TFT_MOSI, TFT_CS);

#if defined(DISPLAY_XTEINK_X3)
    Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
    // A gauge that does not answer must not stall the UI for the default
    // timeout on every redraw.
    Wire.setTimeOut(4);
#endif
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
#if defined(DISPLAY_XTEINK_X3)
    // The gauge already reports a state of charge, so there is no curve to fit.
    // It is also slow to answer over a 400kHz bus, and the launcher asks on
    // every header redraw, so cache it — and on an I2C error keep the last
    // reading rather than blinking to zero.
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
#else
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
#endif
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

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {
    if (launcherGpioRead(PWR_BTN) != LOW) return;

    const uint32_t start = launcherMillis();
    int lastCountDown = -1;
    while (launcherGpioRead(PWR_BTN) == LOW) {
        if (launcherMillis() - start > 500) {
            const int countDown = (launcherMillis() - start) / 1000 + 1;
            if (countDown < 3) {
                // Repainting an e-paper panel per frame is far too slow, so
                // only push a refresh when the number actually changes.
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
