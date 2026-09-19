#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <AXP192.h>
#include <interface.h>
#define SEL_BTN 37
#define DW_BTN 39
AXP192 axp192;

// ---------------------------------------------------------------------------
// Which stick is this?
//
// The StickC and the StickC Plus are the same board with different glass: same
// ESP32-PICO-D4, same AXP192, same buttons, same LCD pins. One binary drives
// both and asks the panel which one it is at boot.
//
// The seeded configuration is the Plus (ST7789, 135x240). The probe only has to
// recognise the StickC.
// ---------------------------------------------------------------------------
enum StickPanel : uint8_t { STICK_PLUS_ST7789 = 0, STICK_C_ST7735 = 1 };
static StickPanel stickPanel = STICK_PLUS_ST7789;

// StickC geometry, from M5GFX's Panel_M5StickC. The ST7735S addresses 132x162
// of RAM behind an 80x160 window, and the window sits 26/1 in from either end,
// so both of Arduino_GFX's per-rotation offset pairs carry the same numbers.
#define STICK_C_WIDTH 80
#define STICK_C_HEIGHT 160
#define STICK_C_COL_OFS 26
#define STICK_C_ROW_OFS 1
// M5GFX gives this panel offset_rotation = 2 and the Plus none: the StickC glass
// is mounted 180 degrees round from the Plus. ROTATION is the Plus's mounting,
// so the StickC's is that plus two.
#define STICK_C_ROTATION ((ROTATION + 2) & 0b11)

/***************************************************************************************
** Function name: _read_panel_id()
** Description:   RDDID (0x04) off the LCD, bit-banged
**
** Neither stick wires a MISO to the panel: the bus is 3-wire, and the answer
** comes back on the pin the host just wrote the command on. M5GFX reads it the
** same way (M5GFX.cpp, the PICO-D4 branch of autodetect) and keys off the first
** ID byte -- 0x7C on an ST7735S, 0x85 (0x81 on some units) on an ST7789.
***************************************************************************************/
static uint8_t _read_panel_id() {
    launcherGpioOutput(TFT_SCLK);
    launcherGpioOutput(TFT_MOSI);
    launcherGpioOutput(TFT_DC);
    launcherGpioOutput(TFT_CS);
    launcherGpioOutput(TFT_RST);
    launcherGpioWrite(TFT_SCLK, LOW);

    // Reset pulse: the ID registers read back reliably from a known state, and
    // tftInit() resets again before it uses the panel for real.
    launcherGpioWrite(TFT_RST, HIGH);
    launcherDelayMs(5);
    launcherGpioWrite(TFT_RST, LOW);
    launcherDelayMs(10);
    launcherGpioWrite(TFT_RST, HIGH);
    launcherDelayMs(50);

    // Mode 0, MSB first. gpio_set_level is slow enough on its own that the
    // clock lands well under the controller's read limit.
    auto writeByte = [](uint8_t b) {
        for (int8_t i = 7; i >= 0; --i) {
            launcherGpioWrite(TFT_MOSI, (b >> i) & 1);
            launcherGpioWrite(TFT_SCLK, HIGH);
            launcherGpioWrite(TFT_SCLK, LOW);
        }
    };

    launcherGpioWrite(TFT_DC, LOW); // command
    launcherGpioWrite(TFT_CS, HIGH);
    writeByte(0x00); // NOP with CS high, to park the controller's shift register
    launcherGpioWrite(TFT_CS, LOW);
    writeByte(0x04); // RDDID

    launcherGpioWrite(TFT_DC, HIGH); // data
    launcherGpioInput(TFT_MOSI);     // the controller drives the line from here on

    // One dummy clock, then the first ID byte -- the only one that separates the
    // two panels. The two after it are model and version and are not read.
    launcherGpioWrite(TFT_SCLK, HIGH);
    launcherGpioWrite(TFT_SCLK, LOW);

    uint8_t id = 0;
    for (int8_t i = 7; i >= 0; --i) {
        launcherGpioWrite(TFT_SCLK, HIGH);
        id |= (launcherGpioRead(TFT_MOSI) & 1) << i;
        launcherGpioWrite(TFT_SCLK, LOW);
    }

    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(TFT_MOSI);
    return id;
}

/***************************************************************************************
** Function name: _detect_panel()
** Description:   pick the panel, and everything else that follows from it
***************************************************************************************/
static void _detect_panel() {
    const uint8_t id = _read_panel_id();
    stickPanel = (id == 0x7C) ? STICK_C_ST7735 : STICK_PLUS_ST7789;

    if (stickPanel == STICK_C_ST7735) {
        displayConfig.driver = 0; // Arduino_ST7735
        displayConfig.width = STICK_C_WIDTH;
        displayConfig.height = STICK_C_HEIGHT;
        displayConfig.colOfs1 = displayConfig.colOfs2 = STICK_C_COL_OFS;
        displayConfig.rowOfs1 = displayConfig.rowOfs2 = STICK_C_ROW_OFS;
        displayConfig.rotation = STICK_C_ROTATION;
        rotation = STICK_C_ROTATION;

        // 80x160 has no room for the Plus's text sizes.
        _fp = 1;
        _fm = 1;
        _fg = 2;

        device_name = "M5Stack StickC";
    }

    launcherConsolePrintf(
        "Panel: %s (RDDID 0x%02X, %dx%d)\n",
        stickPanel == STICK_C_ST7735 ? "ST7735S StickC" : "ST7789 StickC Plus",
        id,
        displayConfig.width,
        displayConfig.height
    );
}

void _setup_gpio() {
    launcherGpioInput(SEL_BTN);
    launcherGpioInput(DW_BTN);
    // https://github.com/pr3y/Bruce/blob/main/media/connections/cc1101_stick_SDCard.jpg
    launcherGpioOutput(33);      // Keeps this pin high to allow working with the following pinout
    launcherGpioWrite(33, HIGH); // Keeps this pin high to allow working with the following pinout
    axp192.begin();              // Start the energy management of AXP192
    _detect_panel();             // needs the AXP192 up: it is what powers the panel
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Description:   finish the StickC's panel, which Arduino_GFX only half brings up
**
** Arduino_ST7735 ships one init table and it is the 7735B one: sleep out, colour
** mode, gamma, display on. The ST7735S in the StickC also needs its frame-rate,
** inversion and power blocks -- GVDD, VGH/VGL, the two opamp stages and VCOM --
** or it accepts every command and develops no image at all. The values are the
** ST7735S "Rcmd1/Rcmd2" set, plus the gamma curve M5GFX picks for this glass.
**
** Sent after begin() rather than as an init table because Arduino_ST7735 takes
** no init table (Arduino_ST7789 does; the 7735 class hardcodes its own). None of
** these registers care that the panel is already out of sleep, and the table
** deliberately carries no reset, MADCTL or COLMOD so that what begin() set up
** survives.
***************************************************************************************/
// clang-format off
static const uint8_t stickc_st7735s_bringup[] = {
    BEGIN_WRITE,
    WRITE_C8_BYTES, 0xB1, 3, 0x01, 0x2C, 0x2D,                   // FRMCTR1, normal mode
    WRITE_C8_BYTES, 0xB2, 3, 0x01, 0x2C, 0x2D,                   // FRMCTR2, idle mode
    WRITE_C8_BYTES, 0xB3, 6, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D, // FRMCTR3, partial mode
    WRITE_C8_D8,    0xB4, 0x07,                                  // INVCTR, no inversion
    WRITE_C8_BYTES, 0xC0, 3, 0xA2, 0x02, 0x84,                   // PWCTR1, GVDD -4.6V, AUTO
    WRITE_C8_D8,    0xC1, 0xC5,                                  // PWCTR2, VGH25/VGSEL/VGH
    WRITE_C8_BYTES, 0xC2, 2, 0x0A, 0x00,                         // PWCTR3, opamp + boost
    WRITE_C8_BYTES, 0xC3, 2, 0x8A, 0x2A,                         // PWCTR4, idle
    WRITE_C8_BYTES, 0xC4, 2, 0x8A, 0xEE,                         // PWCTR5, partial
    WRITE_C8_D8,    0xC5, 0x0E,                                  // VMCTR1, VCOM
    WRITE_C8_BYTES, 0xE0, 16,                                    // GMCTRP1
        0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
        0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
    WRITE_C8_BYTES, 0xE1, 16,                                    // GMCTRN1
        0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
        0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,
    WRITE_C8_D8,    0x26, 0x08,                                  // GAMMASET, curve 4
    END_WRITE,
    DELAY, 10,
    BEGIN_WRITE, WRITE_COMMAND_8, 0x13, END_WRITE,               // NORON
    DELAY, 10,
    BEGIN_WRITE, WRITE_COMMAND_8, 0x29, END_WRITE,               // DISPON
};
// clang-format on

void _post_setup_gpio() {
    if (stickPanel != STICK_C_ST7735) return;
    Arduino_DataBus *bus = tft->dataBus();
    if (bus) { bus->batchOperation(stickc_st7735s_bringup, sizeof(stickc_st7735s_bringup)); }
}

int getBattery() {
    int percent = 0;
    float b = axp192.GetBatVoltage();
    percent = ((b - 3.0) / 1.2) * 100;

    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

void _setBrightness(uint8_t brightval) {
    if (brightval > 100) brightval = 100;
    axp192.ScreenBreath(brightval);
}

void InputHandler(void) {
    static unsigned long tm = 0;
    if (launcherMillis() - tm < 200 && !LongPress) return;

    bool upPressed = (axp192.GetBtnPress());
    bool selPressed = (launcherGpioRead(SEL_BTN) == LOW);
    bool dwPressed = (launcherGpioRead(DW_BTN) == LOW);

    bool anyPressed = upPressed || selPressed || dwPressed;
    if (anyPressed) tm = launcherMillis();
    if (anyPressed && wakeUpScreen()) return;

    AnyKeyPress = anyPressed;
    EscPress = upPressed & dwPressed;
    if (EscPress) return;
    PrevPress = upPressed;
    NextPress = dwPressed;
    SelPress = selPressed;
}

void powerOff() { axp192.PowerOff(); }

void checkReboot() {
    static unsigned long time_count = 0;
    static bool armed = false;
    int countDown;
    /* Long press power off */
    if (axp192.GetBtnPress()) {
        if (armed == false) {
            time_count = launcherMillis();
            armed = true;
            return;
        }
        if (launcherMillis() - time_count < 500) return;

        while (axp192.GetBtnPress()) {
            // Display poweroff bar only if holding button
            if (launcherMillis() - time_count > 500) {
                tft->setCursor(60, 12);
                tft->setTextSize(1);
                tft->setTextColor(FGCOLOR, BGCOLOR);
                countDown = (launcherMillis() - time_count) / 1000 + 1;
                tft->printf(" PWR OFF IN %d/3\n", countDown);
                launcherDelayMs(10);
            }
        }
        // Clear text after releasing the button
        tft->fillRect(60, 12, tftWidth - 60, 8, BGCOLOR);
    }
    armed = false;
}
