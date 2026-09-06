#include "display.h"
#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <IoExpanderXL9555.hpp>
#include <Wire.h>
#include <interface.h>

#define I2C_SDA 39
#define I2C_SCL 40

// XL9555/PCA9535-compatible IO expander. Holds the pins the ESP32-S3 ran out
// of: LCD CS/RESET, touch RESET, and the LoRa radio's CS/RESET/BUSY/DIO1
// (unused here -- Launcher has no LoRa support and never touches them).
#define IO_EXPANDER_ADDR 0x20
#define EXP_LORA_CS 0
#define EXP_LORA_RST 1
#define EXP_LORA_BUSY 2
#define EXP_LORA_DIO1 3
#define EXP_LCD_CS 4
#define EXP_LCD_RST 5
#define EXP_TOUCH_INT 6
#define EXP_TOUCH_RST 7
#define EXP_SENSOR_PWR 8 // powers the RP2040 sensor coprocessor -- unused, no sensor support here

#define BUTTON_PIN 38
#define TOUCH_ADDR 0x48 // FT6336-family, not the SensorLib default 0x5D

// Seeed's own SDK also probes this address as a fallback for the IO
// expander on some board revisions (components/bsp/src/boards/
// sensecap_indicator_board.c, bsp_board_sensecap_indicator_detect()).
#define IO_EXPANDER_ADDR_ALT 0x39

static IoExpanderXL9555 io;
static bool expanderReady = false;

class SensecapPanelInitBus : public Arduino_DataBus {
public:
    bool begin(int32_t speed = SPI_DEFAULT_FREQ, int8_t dataMode = GFX_NOT_DEFINED) override {
        (void)speed;
        (void)dataMode;
        pinMode(41, OUTPUT);
        pinMode(48, OUTPUT);
        digitalWrite(41, HIGH);
        digitalWrite(48, HIGH);
        csHigh();
        return expanderReady;
    }

    void beginWrite() override { csHigh(); }
    void endWrite() override { csHigh(); }
    void writeCommand(uint8_t c) override { write9(c, false); }
    void writeCommand16(uint16_t c) override {
        writeCommand(static_cast<uint8_t>(c >> 8));
        writeCommand(static_cast<uint8_t>(c));
    }
    void writeCommandBytes(uint8_t *data, uint32_t len) override {
        while (len--) writeCommand(*data++);
    }
    void write(uint8_t d) override { write9(d, true); }
    void write16(uint16_t d) override {
        write(static_cast<uint8_t>(d >> 8));
        write(static_cast<uint8_t>(d));
    }
    void writeRepeat(uint16_t p, uint32_t len) override {
        while (len--) write16(p);
    }
    void writeBytes(uint8_t *data, uint32_t len) override {
        while (len--) write(*data++);
    }
    void writePixels(uint16_t *data, uint32_t len) override {
        while (len--) write16(*data++);
    }

private:
    static void csHigh() {
        if (expanderReady) io.digitalWrite(EXP_LCD_CS, HIGH);
    }

    static void csLow() {
        if (expanderReady) io.digitalWrite(EXP_LCD_CS, LOW);
    }

    static void clockBit(bool value) {
        digitalWrite(48, value ? HIGH : LOW);
        digitalWrite(41, HIGH);
        delayMicroseconds(10);
        digitalWrite(41, LOW);
        delayMicroseconds(10);
    }

    static void write9(uint8_t value, bool data) {
        csLow();
        delayMicroseconds(10);
        digitalWrite(41, LOW);
        delayMicroseconds(10);
        clockBit(data);
        for (uint8_t mask = 0x80; mask; mask >>= 1) { clockBit(value & mask); }
        digitalWrite(41, HIGH);
        delayMicroseconds(10);
        digitalWrite(41, LOW);
        delayMicroseconds(10);
        csHigh();
        delayMicroseconds(10);
    }
};

Arduino_DataBus *sensecapInitBus() {
    static SensecapPanelInitBus bus;
    return &bus;
}

static DeviceButtons buttonsCfg() { return DeviceButtons{BUTTON_PIN}; }

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_sda = -1; // Wire already begun below
    cfg.pin_scl = -1;
    cfg.pin_rst = -1; // on the IO expander, reset by hand in _setup_gpio()
    cfg.pin_irq = -1; // also on the IO expander; FT6X36 polls instead of using IRQ
                      // rotation:        0      1      2      3
    bool swapXY[4] = {false, true, false, true};
    bool mirrorX[4] = {false, false, true, true};
    bool mirrorY[4] = {false, true, true, false};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

// Quick bus scan so a serial log always shows what actually answered on
// I2C, regardless of which specific driver init below succeeds or fails.
static void i2cScan() {
    launcherConsolePrintln("I2C scan (SDA=39, SCL=40):");
    uint8_t found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            launcherConsolePrintf("  found device @ 0x%02X\n", addr);
            found++;
        }
    }
    if (!found) launcherConsolePrintln("  no I2C devices answered");
}

void _setup_gpio() {
    launcherConsolePrintf(
        "PSRAM: size=%u free=%u (before display init)\n", ESP.getPsramSize(), ESP.getFreePsram()
    );

    Wire.begin(I2C_SDA, I2C_SCL);
    delay(50);
    i2cScan();

    hal_buttons_init(buttonsCfg(), 1);

    expanderReady = io.begin(Wire, IO_EXPANDER_ADDR);
    if (expanderReady) {
        launcherConsolePrintf("IO expander (XL9555) found @ 0x%02X\n", IO_EXPANDER_ADDR);
    } else {
        expanderReady = io.begin(Wire, IO_EXPANDER_ADDR_ALT);
        if (expanderReady) {
            launcherConsolePrintf(
                "IO expander (XL9555) found @ 0x%02X (alt address)\n", IO_EXPANDER_ADDR_ALT
            );
        }
    }
    if (!expanderReady) {
        launcherConsolePrintln("IO expander NOT found @ 0x20 or 0x39 -- display/touch reset will not run");
        return;
    }

    io.pinMode(EXP_LCD_CS, OUTPUT);
    io.digitalWrite(EXP_LCD_CS, HIGH);

    io.pinMode(EXP_LCD_RST, OUTPUT);
    io.digitalWrite(EXP_LCD_RST, HIGH);

    io.pinMode(EXP_TOUCH_RST, OUTPUT);
    io.digitalWrite(EXP_TOUCH_RST, LOW);
    delay(10);
    io.digitalWrite(EXP_TOUCH_RST, HIGH);
    delay(50);

    launcherConsolePrintln("LCD CS/RESET and touch RESET set via IO expander");
}

void _post_setup_gpio() {
    launcherConsolePrintf(
        "PSRAM: size=%u free=%u (after display init) -- 480x480x2 framebuffer needs ~460800B\n",
        ESP.getPsramSize(),
        ESP.getFreePsram()
    );

    hal_bright_attach(TFT_BL);
    if (hal_touch_init(touchCfg(), TOUCH_ADDR)) {
        launcherConsolePrintf("Touch IC (FT6336 @ 0x%02X) started\n", TOUCH_ADDR);
    } else {
        launcherConsolePrintf("Touch IC (FT6336 @ 0x%02X) NOT started\n", TOUCH_ADDR);
    }
}

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

void InputHandler(void) {
    static bool lastBtn = false;
    hal_buttons_poll_1(buttonsCfg());
    bool btnNow = NextPress || PrevPress || SelPress || EscPress;
    if (btnNow && !lastBtn) {
        launcherConsolePrintf(
            "Button: next=%d prev=%d sel=%d esc=%d\n", NextPress, PrevPress, SelPress, EscPress
        );
    }
    lastBtn = btnNow;

    static long lastTouch = 0;
    if (launcherMillis() - lastTouch > 200) {
        lastTouch = launcherMillis();
        LTouchPoint t;
        if (hal_touch_read(touchCfg(), t)) {
            launcherConsolePrintf("Touch: x=%u y=%u\n", t.x, t.y);
            if (!hal_touch_apply(t)) return;
        }
    }
}
