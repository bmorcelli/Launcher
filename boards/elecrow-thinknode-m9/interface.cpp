#include "hal/bright/bright.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <globals.h>
#include <interface.h>

// #define M9_KB_DEBUG // to enable debug messages

// Keypad coprocessor: drives the whole 5x10 matrix plus the shortcut keys and
// hands the host one key code at a time over I2C, from a single register. The
// register latches the pending key until it is read, and KB_INT is only raised
// for a few keys, so the register is polled -- see InputHandler().
// Two hardware revisions exist and only the I2C address tells them apart --
// which also decides the GPS enable polarity below.
#define KB_ADDR_V1_0 0x6C
#define KB_ADDR_V1_1 0x6D
#define KB_REG_KEY 0x01
#define KB_REG_LONG_PRESS 0x03 // long-press window, big-endian milliseconds
#define KB_REG_STATE 0x06      // write 0x01 to put the companion MCU to sleep
#define KB_LONG_PRESS_MS 700
#define KB_SDA 20
#define KB_SCL 21
#define KB_INT 12
#define KB_LED 46 // keypad backlight, plain GPIO, active HIGH

// Key codes outside the printable ASCII range the coprocessor also reports
#define KEY_DEL 0x08
#define KEY_ENTER 0x0D
#define KEY_FM_LONG 0x86
#define KEY_NONE 0x88 // "no/invalid key", what the register reads when drained
#define KEY_DEL_LONG 0x89
#define KEY_LEFT 0xB4
#define KEY_UP 0xB5
#define KEY_DOWN 0xB6
#define KEY_RIGHT 0xB7

#define LORA_CS 39
#define VEXT_EN 18       // peripheral rail, active LOW, powers the display
#define GPS_EN 11        // polarity depends on the board revision, see below
#define GPS_SLEEP_INT 10 // AG3352 idle_int, must rest HIGH

// TFT_BL is wired active LOW on this board.
static const HalBrightCurve BL_CURVE = {0, 255, 2.2f, true};

static uint8_t kbAddr = 0;

static void kbWrite(uint8_t reg, const uint8_t *data, uint8_t len) {
    if (!kbAddr) return;
    Wire.beginTransmission(kbAddr);
    Wire.write(reg);
    Wire.write(data, len);
    Wire.endTransmission();
}

// The coprocessor does not answer a repeated start: the register address has
// to be sent as its own complete transaction (STOP), then read back. This is
// the same sequence Elecrow's own I2C keypad examples use.
static uint8_t kbRead(uint8_t reg) {
    if (!kbAddr) return 0;
    Wire.beginTransmission(kbAddr);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return 0;
    launcherDelayMs(2);
    if (Wire.requestFrom(kbAddr, (uint8_t)1) != 1) return 0;
    return Wire.read();
}

static bool kbProbe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void _setup_gpio() {
    // Display/peripheral rail first, everything below it is dead without it
    launcherGpioOutput(VEXT_EN);
    launcherGpioWrite(VEXT_EN, LOW);

    // Every CS on the shared SPI bus must be HIGH before the bus comes up
    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(SDCARD_CS);
    launcherGpioWrite(SDCARD_CS, HIGH);
    launcherGpioOutput(LORA_CS);
    launcherGpioWrite(LORA_CS, HIGH);

    launcherGpioOutput(GPS_SLEEP_INT);
    launcherGpioWrite(GPS_SLEEP_INT, HIGH);

    launcherGpioOutput(KB_LED);
    launcherGpioWrite(KB_LED, LOW);
    launcherGpioInput(KB_INT);

    Wire.begin(KB_SDA, KB_SCL);
    if (kbProbe(KB_ADDR_V1_0)) kbAddr = KB_ADDR_V1_0;
    else if (kbProbe(KB_ADDR_V1_1)) kbAddr = KB_ADDR_V1_1;
    else {
        launcherConsolePrintln("Failed to find the keypad coprocessor");
#ifdef M9_KB_DEBUG
        for (uint8_t addr = 0x08; addr < 0x78; addr++)
            if (kbProbe(addr)) launcherConsolePrintf("M9 i2c: device at 0x%02x\n", addr);
#endif
    }

    // The Launcher never uses the GNSS, so keep its core powered down. v1.0
    // enables the module LOW, v1.1 HIGH -- an unidentified board is treated
    // as v1.0, same as the reference firmware does.
    launcherGpioOutput(GPS_EN);
    launcherGpioWrite(GPS_EN, kbAddr == KB_ADDR_V1_1 ? LOW : HIGH);

    const uint8_t longPress[] = {(KB_LONG_PRESS_MS >> 8) & 0xFF, KB_LONG_PRESS_MS & 0xFF};
    kbWrite(KB_REG_LONG_PRESS, longPress, sizeof(longPress));

    // The key register latches, so the key held to boot into the Launcher is
    // still pending here -- drop it instead of acting on it at the first frame.
    kbRead(KB_REG_KEY);

    // setBrightness() runs before _post_setup_gpio(), so attach here
    hal_bright_attach(TFT_BL);
}

void _setBrightness(uint8_t brightval) {
    hal_bright_set(TFT_BL, brightval, BL_CURVE);
    launcherGpioWrite(KB_LED, brightval > 0 ? HIGH : LOW);
}

void InputHandler(void) {
    // taskInputHandler() already calls checkPowerSaveTime() and resetGlobals()
    // (which clears KeyStroke) before every call into here.
    //
    // KB_INT (the schematic's ESP32_WAKEUP) is NOT a per-key interrupt: the
    // coprocessor raises it to wake a sleeping host, not on every key press,
    // so the key register has to be polled. The reference firmware does the
    // same -- it polls every 300ms and only uses the line to cut that wait
    // short. Gating the read on the line instead loses most key presses.
    uint8_t key = kbRead(KB_REG_KEY);
    if (key == KEY_NONE || key == 0x00 || key == 0xFF) return;
#ifdef M9_KB_DEBUG
    launcherConsolePrintf("M9 kb: 0x%02x\n", key);
#endif
    if (!wakeUpScreen()) AnyKeyPress = true;
    else return;

    switch (key) {
        case KEY_UP: UpPress = true; return;
        case KEY_DOWN: DownPress = true; return;
        case KEY_LEFT: PrevPress = true; return;
        case KEY_RIGHT: NextPress = true; return;
        case KEY_FM_LONG: EscPress = true; return;
        default: break;
    }

    if (key == KEY_DEL || key == KEY_DEL_LONG) {
        KeyStroke.del = true;
        KeyStroke.exit_key = true;
        EscPress = true;
        key = KEY_DEL;
    } else if (key == KEY_ENTER) {
        KeyStroke.enter = true;
        SelPress = true;
    } else if (key < 0x20 || key > 0x7E) {
        // Mute/Home/Time/GPS/FM/Preset shortcuts: nothing to map them to yet
        return;
    }
    KeyStroke.hid_keys.push_back(key);
    KeyStroke.word.push_back(key);
    KeyStroke.pressed = true;
}

void powerOff() {
    _setBrightness(0);
    launcherGpioWrite(VEXT_EN, HIGH);
    // Tell the companion MCU to sleep -- it still drives KB_INT high on the
    // next key press, which is what brings the board back up.
    const uint8_t sleep = 0x01;
    kbWrite(KB_REG_STATE, &sleep, 1);
    Wire.end();

    gpio_pulldown_en((gpio_num_t)KB_INT);
    esp_sleep_enable_ext1_wakeup(1ULL << KB_INT, ESP_EXT1_WAKEUP_ANY_HIGH);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
