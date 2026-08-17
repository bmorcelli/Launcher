#include "GaugeBQ27220.hpp"
#include "IoExpanderXL9555.hpp"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SD_MMC.h>
#include <TouchDrv.hpp>
#include <Wire.h>
#include <esp_sleep.h>
#include <interface.h>
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#define BTN_ACT LOW
#define SEL_BTN 35
IoExpanderXL9555 io;
GaugeBQ27220 gauge;
// ---------------------------------------------------------------------------
// Panel variant
//
// The T-Display P4 ships with two different screens behind the same case, and
// one binary drives both. They differ in three things and nothing else:
//
//                 IPS (540x1168)          AMOLED (568x1232)
//   panel         hi8561                  rm69a10
//   touch         HI8561 @ 0x68           GT9895 @ 0x5D, 1060x2400 grid
//   brightness    backlight PWM on 51     panel register 0x51 over DSI
//
// The panel is a runtime choice now (see displayConfig.initOps), so all three
// follow from one probe at boot. The touch controller is what we probe: the
// GT9895 is a separate chip and answers on I2C as soon as the 3V3 rail is up,
// while the HI8561 shares silicon with the display driver and stays quiet
// until the panel is initialized. So an ACK at 0x5D means AMOLED; anything
// else means IPS, which is also the safe default because its numbers are the
// ones the build flags seeded.
// ---------------------------------------------------------------------------
enum P4Panel : uint8_t { P4_PANEL_IPS = 0, P4_PANEL_AMOLED = 1 };
static P4Panel p4Panel = P4_PANEL_IPS;

// Backlight GPIO of the IPS variant. Not a build flag: on the AMOLED unit this
// pin drives nothing, and brightness goes to the panel instead.
#define P4_IPS_BL_PIN 51

// GT9895 digitizer grid, bigger than the panel it sits on.
#define P4_GT9895_RAW_WIDTH 1060
#define P4_GT9895_RAW_HEIGHT 2400

// Both controllers are constructed; only the one the probe picked is begun.
// Everything the launcher calls on them is virtual on TouchDrvInterface, so
// the read path below never has to know which it got.
static TouchDrvGT9895 touchGt9895;
static TouchDrvHI8561 touchHi8561;
static TouchDrvInterface *touch = &touchHi8561;
static bool touchOk = false;
static bool ioOk = false;
static bool gaugeOk = false;
#define SENSOR_IRQ 5 // XL9535 IRQ

enum : uint8_t {
    XL_POWER_EN_3V3 = 0,     // kIo0  - active LOW
    XL_SKY13453_VCTL = 1,    // kIo1  - antenna select, 1 == RF1
    XL_SCREEN_RST = 2,       // kIo2
    XL_TOUCH_RST = 3,        // kIo3
    XL_TOUCH_INT = 4,        // kIo4  - input
    XL_ETHERNET_RST = 5,     // kIo5
    XL_POWER_EN_5V0 = 6,     // kIo6
    XL_ICM20948_INT = 7,     // kIo7  - input
    XL_USB_PHY_POWER_EN = 8, // kIo10
    XL_GPS_WAKEUP = 9,       // kIo11
    XL_RTC_INT = 10,         // kIo12
    XL_ESP32C6_WAKEUP = 11,  // kIo13
    XL_ESP32C6_EN = 12,      // kIo14
    XL_SD_POWER_EN = 13,     // kIo15 - active LOW
    XL_SX1262_RST = 14,      // kIo16
    XL_SX1262_DIO1 = 15,     // kIo17 - input
};

// Reproduces ConfigXl9535() from the LilyGO reference driver
// (t_display_p4_driver.cpp). The rail enables are active LOW and the peripheral
// resets are toggled twice, so the final levels are not all "HIGH" - driving
// kSdPowerEn HIGH keeps the SD card powered off.
static void _config_xl9535() {
    const uint8_t outputs[] = {
        XL_SCREEN_RST,
        XL_TOUCH_RST,
        XL_USB_PHY_POWER_EN,
        XL_POWER_EN_5V0,
        XL_POWER_EN_3V3,
        XL_GPS_WAKEUP,
        XL_ESP32C6_EN,
        XL_ETHERNET_RST,
        XL_SD_POWER_EN,
        XL_SX1262_RST,
        XL_SKY13453_VCTL,
    };
    for (auto pin : outputs) io.pinMode(pin, OUTPUT);
    io.pinMode(XL_ICM20948_INT, INPUT);
    io.pinMode(XL_SX1262_DIO1, INPUT);
    io.pinMode(XL_TOUCH_INT, INPUT);

    io.digitalWrite(XL_USB_PHY_POWER_EN, LOW);
    io.digitalWrite(XL_SKY13453_VCTL, HIGH); // RF1 antenna

    io.digitalWrite(XL_POWER_EN_3V3, LOW);
    delay(10);
    io.digitalWrite(XL_POWER_EN_3V3, HIGH);
    delay(500);
    io.digitalWrite(XL_POWER_EN_3V3, LOW);
    delay(10);

    // Two reset pulses, exactly as the vendor driver does.
    for (int pass = 0; pass < 3; pass++) {
        const uint8_t level = (pass == 1) ? LOW : HIGH;
        io.digitalWrite(XL_SCREEN_RST, level);
        io.digitalWrite(XL_TOUCH_RST, level);
        io.digitalWrite(XL_ESP32C6_EN, level);
        io.digitalWrite(XL_ETHERNET_RST, level);
        io.digitalWrite(XL_GPS_WAKEUP, level);
        io.digitalWrite(XL_SX1262_RST, level);
        io.digitalWrite(XL_SD_POWER_EN, level == HIGH ? LOW : HIGH);
        io.digitalWrite(XL_POWER_EN_5V0, level == HIGH ? HIGH : LOW);
        delay(pass == 2 ? 120 : 10);
    }
}

/***************************************************************************************
** T-Display-P4 Keyboard add-on
**
** The keyboard is a separate board that plugs into the 1x4 / 2x8 headers, so it may or
** may not be present at boot and can be attached later. Everything it carries lives on
** its own I2C bus (Wire1, the "port 3" pins) so the main IIC_1 bus is left untouched:
**
**   SDA = GPIO46   SCL = GPIO45   TCA8418 INT = GPIO48   backlight (SY7200A) = GPIO47
**   XL9555 @ 0x20 (LEDs + TCA8418 reset)     TCA8418 @ 0x34 (10x7 key matrix)
**
** Pin map and init sequence come from the LilyGO reference driver
** (t_display_p4_keyboard_config.h / TDisplayP4Driver::InitKeyboard()).
***************************************************************************************/
#define KB_I2C_SDA 46
#define KB_I2C_SCL 45
#define KB_INT_PIN 48
#define KB_BL_PIN 47
#define KB_XL9555_ADDR 0x20
#define KB_TCA8418_ADDR 0x34

// XL9555 (keyboard board) port-0 bits.
enum : uint8_t {
    KBX_TMIX_RF_EN = 0,
    KBX_RF_SWITCH0 = 1,
    KBX_RF_SWITCH1 = 2,
    KBX_LED1 = 3,
    KBX_LED2 = 4,
    KBX_LED3 = 5,
    KBX_TCA8418_RST = 6,
};

// CS lines of the radios on the 2x8 header. They share one SPI bus with everything else
// on that connector, so they are parked HIGH (deselected) and never driven again.
#define KB_CS_ST25R3916 27
#define KB_CS_NRF24L01 54
#define KB_CS_CC1101 36

// TCA8418 registers used here.
#define TCA_REG_CFG 0x01
#define TCA_REG_INT_STAT 0x02
#define TCA_REG_KEY_LCK_EC 0x03
#define TCA_REG_KEY_EVENT_A 0x04
#define TCA_REG_GPIO_INT_STAT1 0x11
#define TCA_REG_KP_GPIO1 0x1D

// CFG: AI | OVR_FLOW_M | OVR_FLOW_IEN | KE_IEN. The overflow interrupt is enabled on
// purpose: a full FIFO silently drops events, and the one event we cannot afford to lose
// is the release of a held key - without it auto-repeat never stops.
#define TCA_CFG_VALUE 0xA9
#define TCA_INT_KEY_EVENTS 0x01
#define TCA_INT_OVERFLOW 0x08
#define TCA_INT_ALL 0x1F

// A key reported as held for longer than this without a release is assumed lost. Real
// holds are legitimate (scrolling a list), so this is deliberately generous - it is the
// backstop that guarantees the UI always recovers, not the primary mechanism.
#define KB_HOLD_MAX_MS 5000

// Key event numbers (num = row * 10 + col + 1) of the non-printable keys.
enum : uint8_t {
    KBK_ESC_A = 11,
    KBK_ESC_B = 12,
    KBK_CAPS = 31,
    KBK_ALT = 41,
    KBK_CTRL = 49,
    KBK_UP = 50,
    KBK_FN_A = 51,
    KBK_WIN = 52,
    KBK_SHIFT = 53,
    KBK_TAB = 54,
    KBK_FN_B = 58,
    KBK_LEFT = 59,
    KBK_DOWN = 60,
    KBK_DEL = 63,
    KBK_ENTER_A = 64,
    KBK_RECORD = 65,
    KBK_ENTER_B = 66,
    KBK_RIGHT = 68,
};

#define KB_KEY_COUNT 68

// Printable characters indexed by (event number - 1); '\0' means "not a character key".
// Same layout as keyboard::device::tca8418::kMap in the reference driver.
static const char _kbBase[KB_KEY_COUNT] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   // F1..F10
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', // Esc Esc 1..8
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', //
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', // Caps a..l
    0,   'z', 'x', 'c', 'v', 'b', 'n', 'm', 0,   0,   // Alt z..m Ctrl Up
    0,   0,   0,   0,   ' ', ' ', ' ', 0,   0,   0,   // Fn Win Shift Tab Space*3 Fn Left Down
    0,   '9', 0,   0,   0,   0,   '0', 0,             // F11 9 Del Enter Record Enter 0 Right
};

// Shifted characters for the number row; letters are handled by toupper().
static char _kbShiftDigit(char c) {
    switch (c) {
        case '1': return '!';
        case '2': return '@';
        case '3': return '#';
        case '4': return '$';
        case '5': return '%';
        case '6': return '^';
        case '7': return '&';
        case '8': return '*';
        case '9': return '(';
        case '0': return ')';
        default: return c;
    }
}

static bool kbReady = false;        // TCA8418 answered and is configured
static volatile bool kbIrq = false; // set by the GPIO48 ISR
static bool kbShift = false, kbCaps = false, kbFn = false, kbCtrl = false, kbAlt = false;
static bool kbClearHeld = false; // drop stale "held" keys after a (re)connect
// Auto-repeat state.
static bool nextHeld = false, prevHeld = false, upHeld = false, downHeld = false;
static unsigned long nextHoldStart = 0, prevHoldStart = 0, upHoldStart = 0, downHoldStart = 0;
static uint32_t kbNextProbe = 0; // hot-plug retry timestamp

constexpr unsigned long KB_REPEAT_START_MS = 350;
constexpr unsigned long KB_REPEAT_MS = 150;
constexpr uint32_t KB_PROBE_INTERVAL_MS = 2000;

static void IRAM_ATTR _kbIsr() { kbIrq = true; }

static bool _kbWrite8(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(addr);
    Wire1.write(reg);
    Wire1.write(val);
    return Wire1.endTransmission() == 0;
}

static bool _kbRead8(uint8_t addr, uint8_t reg, uint8_t &val) {
    Wire1.beginTransmission(addr);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false;
    if (Wire1.requestFrom((int)addr, 1) != 1) return false;
    val = Wire1.read();
    return true;
}

// The keyboard XL9555 only drives outputs on port 0 (IO0..IO6); port 1 is left as input.
static bool _kbConfigXl9555() {
    bool ok = true;
    // Outputs first, so the pins take their level the moment the direction flips.
    // LEDs are active LOW (the vendor writes 1 to turn them off) and kTMixRfEn is
    // written 1 exactly as ConfigXl9555() does before touching the RF add-on.
    ok &= _kbWrite8(
        KB_XL9555_ADDR,
        0x02,
        (1 << KBX_LED1) | (1 << KBX_LED2) | (1 << KBX_LED3) | (1 << KBX_TMIX_RF_EN) | (1 << KBX_TCA8418_RST)
    );
    ok &= _kbWrite8(KB_XL9555_ADDR, 0x06, 0x80); // IO0..IO6 output, IO7 input
    ok &= _kbWrite8(KB_XL9555_ADDR, 0x07, 0xFF); // port 1 unused -> input
    if (!ok) return false;

    // TCA8418 reset pulse: high, low, high (ConfigXl9555()).
    const uint8_t base = (1 << KBX_LED1) | (1 << KBX_LED2) | (1 << KBX_LED3) | (1 << KBX_TMIX_RF_EN);
    ok &= _kbWrite8(KB_XL9555_ADDR, 0x02, base | (1 << KBX_TCA8418_RST));
    delay(10);
    ok &= _kbWrite8(KB_XL9555_ADDR, 0x02, base);
    delay(10);
    ok &= _kbWrite8(KB_XL9555_ADDR, 0x02, base | (1 << KBX_TCA8418_RST));
    delay(10);
    return ok;
}

// Drop whatever the FIFO and the status registers were holding after the reset.
static void _kbFlush() {
    uint8_t v = 0;
    for (uint8_t i = 0; i < 10; i++) {
        if (!_kbRead8(KB_TCA8418_ADDR, TCA_REG_KEY_EVENT_A, v) || v == 0) break;
    }
    for (uint8_t i = 0; i < 3; i++) _kbRead8(KB_TCA8418_ADDR, TCA_REG_GPIO_INT_STAT1 + i, v);
    _kbWrite8(KB_TCA8418_ADDR, TCA_REG_INT_STAT, TCA_INT_ALL);
}

static bool _kbInitTca8418() {
    uint8_t probe = 0;
    if (!_kbRead8(KB_TCA8418_ADDR, TCA_REG_CFG, probe)) return false;

    bool ok = true;
    // 10 columns x 7 rows join the key matrix: ROW0..ROW6 (bits 0-6) and COL0..COL9
    // (bits 8-17), i.e. mask 0x0003FF7F spread over KP_GPIO1..3.
    ok &= _kbWrite8(KB_TCA8418_ADDR, TCA_REG_KP_GPIO1 + 0, 0x7F);
    ok &= _kbWrite8(KB_TCA8418_ADDR, TCA_REG_KP_GPIO1 + 1, 0xFF);
    ok &= _kbWrite8(KB_TCA8418_ADDR, TCA_REG_KP_GPIO1 + 2, 0x03);
    ok &= _kbWrite8(KB_TCA8418_ADDR, TCA_REG_CFG, TCA_CFG_VALUE);
    if (!ok) return false;

    _kbFlush();
    return true;
}

// Park the radio CS lines of the add-on so they never pull the shared SPI bus around.
static void _kbParkRadioCs() {
    const uint8_t cs[] = {KB_CS_ST25R3916, KB_CS_NRF24L01, KB_CS_CC1101};
    for (auto pin : cs) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
}

static bool _kbBegin() {
    if (!_kbConfigXl9555()) return false; // no expander -> no keyboard attached
    if (!_kbInitTca8418()) return false;

    kbShift = kbCaps = kbFn = kbCtrl = kbAlt = false;
    kbIrq = false;
    // Unplugging mid-press leaves _kbPoll()'s held flags set; clear them or the key would
    // repeat forever once the board comes back.
    kbClearHeld = true;
    // Reserve once, here, where allocating is safe. After this the input path never grows a
    // vector, so it never holds the heap lock - see the note in _kbPoll().
    KeyStroke.word.reserve(16);
    KeyStroke.hid_keys.reserve(16);
    KeyStroke.modifier_keys.reserve(16);
    analogWrite(KB_BL_PIN, 77); // ~30%, the level the reference driver fades up to
    return true;
}

static void _kbSetup() {
    _kbParkRadioCs();

    Wire1.begin(KB_I2C_SDA, KB_I2C_SCL, 400000);
    pinMode(KB_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(KB_INT_PIN), _kbIsr, FALLING);

    kbReady = _kbBegin();
    if (kbReady) Serial.println("Keyboard (TCA8418) ready");
    else {
        Serial.println("Keyboard absent, will keep probing");
        kbNextProbe = launcherMillis() + KB_PROBE_INTERVAL_MS;
    }
}

// Translate one key event into the navigation globals / KeyStroke.
// Returns false when the I2C read failed, meaning the board was unplugged.
static bool _kbPoll() {
    static unsigned long nextRepeatTime = 0, prevRepeatTime = 0;
    static unsigned long upRepeatTime = 0, downRepeatTime = 0;

    if (kbClearHeld) {
        kbClearHeld = false;
        nextHeld = prevHeld = upHeld = downHeld = false;
    }

    bool nextPulse = false, prevPulse = false, upPulse = false, downPulse = false;
    bool selPulse = false, escPulse = false, keyPulse = false;

    // Deliberately NOT a keyStroke: that struct holds std::vectors, and pushing into one here
    // would make this task allocate on every key event. A plain array keeps the drain
    // heap-free; KeyStroke is only touched at the end, into capacity reserved in _kbBegin().
    char pendingChars[10];
    uint8_t pendingCharCount = 0;
    bool pendingEnter = false, pendingDel = false, pendingExit = false;

    // Three ways in, because losing a release event is what makes a key repeat forever:
    //  - the ISR fired (normal path);
    //  - INT is level driven while events are pending, so a missed edge still shows here;
    //  - a key is currently held, so its release MUST be seen even if both above failed.
    const bool anyHeld = nextHeld || prevHeld || upHeld || downHeld;
    if (kbIrq || digitalRead(KB_INT_PIN) == LOW || anyHeld) {
        kbIrq = false;

        uint8_t intStat = 0;
        if (!_kbRead8(KB_TCA8418_ADDR, TCA_REG_INT_STAT, intStat)) return false;

        // Clear the flags BEFORE reading the FIFO. Clearing afterwards races with events
        // landing mid-drain: the write would wipe a flag belonging to an event still sitting
        // in the FIFO, and that event would never be fetched again. Clearing first means such
        // an event simply re-raises INT.
        _kbWrite8(KB_TCA8418_ADDR, TCA_REG_INT_STAT, intStat & (TCA_INT_KEY_EVENTS | TCA_INT_OVERFLOW));

        // The FIFO is only 10 deep and overwrites its oldest entry when full, so an overflow
        // means events were destroyed inside the chip - quite possibly the release of a key we
        // still believe is down. Everything we think is held is unreliable from here on.
        if (intStat & TCA_INT_OVERFLOW) nextHeld = prevHeld = upHeld = downHeld = false;

        uint8_t count = 0;
        if (!_kbRead8(KB_TCA8418_ADDR, TCA_REG_KEY_LCK_EC, count)) return false;
        count &= 0x0F;
        if (count > 10) count = 10;

        for (uint8_t i = 0; i < count; i++) {
            uint8_t ev = 0;
            if (!_kbRead8(KB_TCA8418_ADDR, TCA_REG_KEY_EVENT_A, ev)) return false;
            const uint8_t num = ev & 0x7F;
            const bool pressed = (ev & 0x80) != 0;
            if (num == 0 || num > KB_KEY_COUNT) continue;

            switch (num) {
                case KBK_SHIFT: kbShift = pressed; continue;
                case KBK_CTRL: kbCtrl = pressed; continue;
                case KBK_ALT: kbAlt = pressed; continue;
                case KBK_FN_A:
                case KBK_FN_B: kbFn = pressed; continue;
                case KBK_CAPS:
                    if (pressed) kbCaps = !kbCaps;
                    continue;
                case KBK_UP:
                    upHeld = pressed;
                    if (pressed) {
                        upPulse = true;
                        upHoldStart = launcherMillis();
                        upRepeatTime = upHoldStart + KB_REPEAT_START_MS;
                    }
                    continue;
                case KBK_DOWN:
                    downHeld = pressed;
                    if (pressed) {
                        downPulse = true;
                        downHoldStart = launcherMillis();
                        downRepeatTime = downHoldStart + KB_REPEAT_START_MS;
                    }
                    continue;
                case KBK_LEFT:
                    prevHeld = pressed;
                    if (pressed) {
                        prevPulse = true;
                        prevHoldStart = launcherMillis();
                        prevRepeatTime = prevHoldStart + KB_REPEAT_START_MS;
                    }
                    continue;
                case KBK_RIGHT:
                    nextHeld = pressed;
                    if (pressed) {
                        nextPulse = true;
                        nextHoldStart = launcherMillis();
                        nextRepeatTime = nextHoldStart + KB_REPEAT_START_MS;
                    }
                    continue;
                case KBK_ENTER_A:
                case KBK_ENTER_B:
                    if (pressed) {
                        pendingEnter = true;
                        selPulse = true;
                        keyPulse = true;
                    }
                    continue;
                case KBK_ESC_A:
                case KBK_ESC_B:
                    if (pressed) {
                        pendingExit = true;
                        escPulse = true;
                        keyPulse = true;
                    }
                    continue;
                case KBK_DEL:
                    if (pressed) {
                        pendingDel = true;
                        pendingExit = true;
                        escPulse = true;
                        keyPulse = true;
                    }
                    continue;
                default: break;
            }

            if (!pressed) continue;
            char c = _kbBase[num - 1];
            if (c == '\0') continue; // F-keys, Win, Tab, Record: nothing to type
            if (c >= 'a' && c <= 'z') {
                if (kbShift != kbCaps) c = c - 'a' + 'A';
            } else if (kbShift) c = _kbShiftDigit(c);

            if (pendingCharCount < sizeof(pendingChars)) pendingChars[pendingCharCount++] = c;
            keyPulse = true;
        }
    }

    const unsigned long now = launcherMillis();

    // Backstop: if a release was destroyed by a FIFO overflow that we somehow did not see,
    // the key would repeat forever and make the UI unusable. Time the hold out instead.
    if (nextHeld && now - nextHoldStart > KB_HOLD_MAX_MS) nextHeld = false;
    if (prevHeld && now - prevHoldStart > KB_HOLD_MAX_MS) prevHeld = false;
    if (upHeld && now - upHoldStart > KB_HOLD_MAX_MS) upHeld = false;
    if (downHeld && now - downHoldStart > KB_HOLD_MAX_MS) downHeld = false;

    if (nextHeld && now >= nextRepeatTime) {
        nextPulse = true;
        nextRepeatTime = now + KB_REPEAT_MS;
    }
    if (prevHeld && now >= prevRepeatTime) {
        prevPulse = true;
        prevRepeatTime = now + KB_REPEAT_MS;
    }
    if (upHeld && now >= upRepeatTime) {
        upPulse = true;
        upRepeatTime = now + KB_REPEAT_MS;
    }
    if (downHeld && now >= downRepeatTime) {
        downPulse = true;
        downRepeatTime = now + KB_REPEAT_MS;
    }

    if (!nextPulse && !prevPulse && !upPulse && !downPulse && !selPulse && !escPulse && !keyPulse)
        return true;

    // A key pressed while the screen sleeps only wakes it up.
    if (wakeUpScreen()) {
        AnyKeyPress = true;
        return true;
    }

    // Writing straight into KeyStroke instead of copy-assigning a temporary. clear() keeps the
    // capacity reserved in _kbBegin(), and push_back stays inside it, so none of this allocates.
    if (keyPulse) {
        KeyStroke.Clear();
        KeyStroke.pressed = true;
        KeyStroke.enter = pendingEnter;
        KeyStroke.del = pendingDel;
        KeyStroke.exit_key = pendingExit;
        KeyStroke.fn = kbFn;
        for (uint8_t i = 0; i < pendingCharCount; i++) KeyStroke.word.push_back(pendingChars[i]);
    } else KeyStroke.Clear();

    AnyKeyPress = true;
    NextPress = nextPulse;
    PrevPress = prevPulse;
    UpPress = upPulse;
    DownPress = downPulse;
    SelPress = selPulse;
    EscPress = escPulse;
    return true;
}

// Called from InputHandler(): drain the keyboard, or look for one that got plugged in.
// The TCA8418 only raises INT once *we* have configured its matrix, so a hot-plug cannot
// be detected from the INT line - it needs an occasional I2C probe instead.
static void _kbHandle() {
    if (kbReady) {
        if (_kbPoll()) return;
        Serial.println("Keyboard lost");
        kbReady = false;
        analogWrite(KB_BL_PIN, 0);
        kbNextProbe = launcherMillis() + KB_PROBE_INTERVAL_MS;
        return;
    }

    if ((int32_t)(launcherMillis() - kbNextProbe) < 0) return;
    kbNextProbe = launcherMillis() + KB_PROBE_INTERVAL_MS;
    kbReady = _kbBegin();
    if (kbReady) Serial.println("Keyboard (TCA8418) connected");
}

static void _gauge_task(void *); // defined next to getBattery()

// The SDMMC-slot-0 pins (SDIO_1_*, shared by both the SD_MMC and the SPI-mode SD
// path below) sit in an IO domain fed by an on-chip LDO on the P4
// (SOC_SDMMC_IO_POWER_EXTERNAL) - without powering it, the pins have no proper
// drive and a card never responds, even though SPI-level init reports success.
// SD_MMC.begin() normally does this itself via BOARD_SDMMC_POWER_CHANNEL, but
// the plain SD/SPIClass path used here knows nothing about it, so it has to be
// powered by hand, once, before anything touches those pins.
static void _powerSdCardIoLdo() {
    sd_pwr_ctrl_ldo_config_t ldoConfig = {.ldo_chan_id = BOARD_SDMMC_POWER_CHANNEL};
    sd_pwr_ctrl_handle_t handle = nullptr;
    if (sd_pwr_ctrl_new_on_chip_ldo(&ldoConfig, &handle) != ESP_OK) {
        Serial.println("SD card IO LDO: failed to acquire channel");
        return;
    }
    if (sd_pwr_ctrl_set_io_voltage(handle, 3300) != ESP_OK) {
        Serial.println("SD card IO LDO: failed to set 3.3V");
    }
}
static bool _i2cAcks(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// Decide which screen this unit has, and point displayConfig at it. Runs from
// _setup_gpio(), which main.cpp calls before tft->begin() -- the window where
// the struct is still writable and nothing has been built from it yet.
//
// Only the AMOLED case writes anything: the build flags already seeded the IPS
// panel, so an undetected board comes up as the more common variant rather
// than as nothing at all.
static void _detect_panel() {
    p4Panel = _i2cAcks(GT9895_SLAVE_ADDRESS_L) ? P4_PANEL_AMOLED : P4_PANEL_IPS;

    if (p4Panel == P4_PANEL_AMOLED) {
        TFT_SET_DSI_INIT(rm69a10_amoled_init_operations);
        displayConfig.width = 568;
        displayConfig.height = 1232;
        displayConfig.hsyncPulseWidth = 50;
        displayConfig.hsyncBackPorch = 150;
        displayConfig.hsyncFrontPorch = 50;
        displayConfig.vsyncPulseWidth = 40;
        displayConfig.vsyncBackPorch = 120;
        displayConfig.vsyncFrontPorch = 80;
    }

    launcherConsolePrintf(
        "Panel: %s (%dx%d)\n",
        p4Panel == P4_PANEL_AMOLED ? "rm69a10 AMOLED" : "hi8561 IPS",
        displayConfig.width,
        displayConfig.height
    );
}

// extern "C" void launcherWifiResetSdioCoprocessor() {
//     if (!ioOk) return;
//     io.digitalWrite(XL_ESP32C6_EN, LOW);
//     delay(50);
//     io.digitalWrite(XL_ESP32C6_EN, HIGH);
//     delay(1000);
// }
void _setup_gpio() {
    Serial.println("Start GPIO");
    _powerSdCardIoLdo();
    // Inicializar GPIO
    pinMode(SENSOR_IRQ, INPUT_PULLUP);
    pinMode(35, INPUT);
    Serial.println("2");
    // Inicializar IO Expander
    const uint8_t chip_address = XL9555_UNKNOWN_ADDRESS;
    Wire.begin(IIC_1_SDA, IIC_1_SCL);
    // Bound every transaction on this bus. The expander, the touch controller and the fuel
    // gauge share it across two tasks, and a slave that stops acknowledging would otherwise
    // block the caller forever - which is exactly how a stuck gauge read froze the UI.
    Wire.setTimeOut(50);
    ioOk = io.begin(Wire, chip_address, IIC_1_SDA, IIC_1_SCL);
    if (!ioOk) { Serial.println("Failed to find XL9555 - check your wiring!"); }
    io.configPins(IoExpanderXL9555::PORT_ALL, INPUT);

    if (ioOk) {
        _config_xl9535();
        Serial.println("XL9535 configured (rails on, C6 out of reset)");
        const uint16_t port = io.digitalReadPort();
        launcherConsolePrintf(
            "XL9535 port=0x%04X | 3v3En=%d(exp0) 5v0En=%d(exp1) SdPwrEn=%d(exp0) C6En=%d(exp1) "
            "ScrRst=%d(exp1) TouchRst=%d(exp1)\n",
            port,
            (port >> XL_POWER_EN_3V3) & 1,
            (port >> XL_POWER_EN_5V0) & 1,
            (port >> XL_SD_POWER_EN) & 1,
            (port >> XL_ESP32C6_EN) & 1,
            (port >> XL_SCREEN_RST) & 1,
            (port >> XL_TOUCH_RST) & 1
        );
    }
    launcherDelayMs(50); // time to start Touch driver
    // After _config_xl9535(): the probe needs the 3V3 rail up and the touch
    // controller out of reset, and it has to happen before tft->begin().
    _detect_panel();

    // BQ27220 fuel gauge, same I2C bus as the expander.
    gaugeOk = gauge.begin(Wire, IIC_1_SDA, IIC_1_SCL);
    Serial.println(gaugeOk ? "BQ27220 gauge started" : "BQ27220 gauge not found");
    // Polled off the UI thread on purpose - see the note above getBattery(). Lowest priority
    // and a small stack: it does one I2C read every 5 s and nothing else depends on it.
    if (gaugeOk) xTaskCreate(_gauge_task, "gauge", 3072, nullptr, 1, nullptr);

    // Last, so nothing started above can take the radio CS lines back off HIGH.
    _kbSetup();
    Serial.println("Finish setup GPIO");
}

// The HI8561's touch reset and interrupt lines hang off the XL9535, not off P4
// GPIOs, so SensorLib needs a way to reach them. It supports that through GPIO
// callbacks: pins with bit 7 set are routed to the expander, everything else
// falls through to the normal Arduino calls. Same convention the LilyGO
// TouchDrv_HI8561_GetPoint_LilyGoP4 example uses.
#define EXP_PIN(n) ((n) | 0x80)

static void _touchPinMode(uint8_t gpio, uint8_t mode) {
    if (gpio & 0x80) io.pinMode(gpio & 0x7F, mode);
    else pinMode(gpio, mode);
}

static void _touchDigitalWrite(uint8_t gpio, uint8_t level) {
    if (gpio & 0x80) io.digitalWrite(gpio & 0x7F, level);
    else digitalWrite(gpio, level);
}

static uint8_t _touchDigitalRead(uint8_t gpio) {
    if (gpio & 0x80) return io.digitalRead(gpio & 0x7F);
    return digitalRead(gpio);
}

void _post_setup_gpio() {
    // Touch has to come up *after* the panel: on this family the touch
    // controller shares silicon with the display driver and does not answer on
    // I2C until the display side is initialized. main.cpp calls tft->begin()
    // before _post_setup_gpio(), which is exactly the window we need.
    // Which controller this is was settled back in _setup_gpio(), by the same
    // probe that chose the panel -- they come as a pair.
    touch = (p4Panel == P4_PANEL_AMOLED) ? static_cast<TouchDrvInterface *>(&touchGt9895)
                                         : static_cast<TouchDrvInterface *>(&touchHi8561);
    touch->setGpioCallback(_touchPinMode, _touchDigitalWrite, _touchDigitalRead);

    if (p4Panel == P4_PANEL_AMOLED) {
        // The GT9895 needs no reset line driven by us: the reference driver
        // builds it with rst = -1 and relies on the XL9535 sequence that
        // already pulses kTouchRst in _config_xl9535(). Only the interrupt is
        // wired up here.
        touch->setPins(-1, EXP_PIN(XL_TOUCH_INT));
        touchOk = touch->begin(Wire, GT9895_SLAVE_ADDRESS_L, IIC_1_SDA, IIC_1_SCL);
    } else {
        touch->setPins(EXP_PIN(XL_TOUCH_RST), EXP_PIN(XL_TOUCH_INT));
        touchOk = touch->begin(Wire, HI8561_SLAVE_ADDRESS, IIC_1_SDA, IIC_1_SCL);
    }
    if (!touchOk) {
        Serial.println("Touch controller not found");
        return;
    }

    if (p4Panel == P4_PANEL_AMOLED) {
        // The GT9895 digitizer grid is bigger than the panel, so the raw
        // readings have to be scaled instead of just clamped. Declaring the raw
        // size first lets setTargetResolution() work out the factors (the
        // vendor driver hands the same ratio to Gt9895 as kXScaleFactor /
        // kYScaleFactor).
        touch->setResolution(P4_GT9895_RAW_WIDTH, P4_GT9895_RAW_HEIGHT);
        touch->setTargetResolution(displayConfig.width, displayConfig.height);
    } else {
        // HI8561 already reports in panel coordinates - this only sets the clamp.
        touch->setMaxCoordinates(displayConfig.width, displayConfig.height);
    }
    launcherConsolePrintf("Touch started: %s (chip ID 0x%X)\n", touch->getModelName(), touch->getChipID());

    if (gaugeOk && gauge.refresh()) {
        launcherConsolePrintf(
            "Battery: %d%% (%u mV, %d mA)\n", getBattery(), gauge.getVoltage(), gauge.getCurrent()
        );
    }
    delay(500);
    launcherConsolePrintln("Starting WIFI");
    if (!launcherWifiInitSdioAuto(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST)) {
        launcherConsolePrintln("WIFI unavailable");
    }
}

void _setBrightness(uint8_t brightval) {
    if (brightval > 100) brightval = 100;
    if (p4Panel == P4_PANEL_IPS) {
        if (brightval == 0) {
            analogWrite(P4_IPS_BL_PIN, brightval);
        } else {
            const uint8_t PWM_MIN = 85;
            const uint8_t PWM_MAX = 255;
            float linear = (float)brightval / 100.0;
            uint8_t value = PWM_MIN + round(pow(linear, 2.2) * (PWM_MAX - PWM_MIN));
            analogWrite(P4_IPS_BL_PIN, value);
        }
    } else {
        // No backlight rail on the AMOLED: brightness is the panel's own 0x51
        // register, written over the DSI link.
        Arduino_ESP32DSIPanel *dsi = tft->dataBus();
        if (dsi) {
            const uint8_t level = (uint8_t)((brightval * 255) / 100);
            dsi->writeCommand(0x51, &level, 1);
        }
    }
    if (kbReady) { analogWrite(KB_BL_PIN, (brightval * 255) / 100); }
}

void InputHandler(void) {
    _kbHandle();

    if (launcherGpioRead(SEL_BTN) == BTN_ACT) {
        SelPress = true;
        AnyKeyPress = true;
        return;
    }

    if (!touchOk) return;

    static long lastRead = launcherMillis();
    if (launcherMillis() - lastRead <= 200 && !LongPress) return;
    lastRead = launcherMillis();

    const TouchPoints &points = touch->getTouchPoints();
    if (!points.hasPoints()) {
        touchPoint.pressed = false;
        return;
    }

    const TouchPoint &p = points.getPoint(0);

    if (wakeUpScreen()) return;
    AnyKeyPress = true;

    // Native panel size, not the TFT_* macros: those describe the IPS variant
    // only, and this binary also runs on the taller AMOLED one.
    const uint16_t panelW = displayConfig.width;
    const uint16_t panelH = displayConfig.height;

    const uint16_t nx = p.x; // native, 0..panelW-1
    const uint16_t ny = p.y; // native, 0..panelH-1
    uint16_t sx, sy;
    switch (rotation) {
        case 1:
            sx = ny;
            sy = (panelW - 1) - nx;
            break;
        case 2:
            sx = (panelW - 1) - nx;
            sy = (panelH - 1) - ny;
            break;
        case 3:
            sx = (panelH - 1) - ny;
            sy = nx;
            break;
        default: // rotation 0 - native orientation, confirmed working on hardware
            sx = nx;
            sy = ny;
            break;
    }

    // launcherConsolePrintf(
    //     "Touch rot=%d native(%d,%d) -> screen(%d,%d) [%dx%d]\n",
    //     rotation,
    //     nx,
    //     ny,
    //     sx,
    //     sy,
    //     (rotation & 1) ? TFT_HEIGHT : TFT_WIDTH,
    //     (rotation & 1) ? TFT_WIDTH : TFT_HEIGHT
    // );

    touchPoint.x = sx;
    touchPoint.y = sy;
    touchPoint.pressed = true;
    touchHeatMap(touchPoint);
}

/*********************************************************************
** Function: getBattery
** location: display.cpp
** Delivers the battery value from 0-100
**********************************************************************/
/*********************************************************************
** Battery
**
** drawMainMenu() calls getBattery() on every redraw, and keyboard navigation redraws many
** times per second. Reading the BQ27220 there put a blocking I2C transaction straight into
** the draw path: when a read wedged inside SensorLib (confirmed on hardware - the loop task
** entered gauge.refresh() and never came back, while the input task kept using the same bus
** happily), the UI died with it and nothing could recover it.
**
** So the gauge no longer runs on the UI thread at all. A dedicated low-priority task polls
** it and publishes the result; getBattery() just returns that value and never touches I2C.
** A wedged gauge now costs a stale percentage instead of a frozen launcher.
**********************************************************************/
static volatile int gaugePercent = 0;

static void _gauge_task(void *) {
    for (;;) {
        if (gaugeOk) {
            if (gauge.refresh()) {
                const int percent = gauge.getStateOfCharge();
                gaugePercent = (percent < 0) ? 0 : (percent > 100) ? 100 : percent;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

int getBattery() { return gaugePercent; }

static void _peripherals_power_down() {
    SD_MMC.end();
    if (kbReady) analogWrite(KB_BL_PIN, 0);
    if (p4Panel == P4_PANEL_IPS) analogWrite(P4_IPS_BL_PIN, 0);
    if (!ioOk) return;
    // io.digitalWrite(XL_SCREEN_RST, LOW);
    io.digitalWrite(XL_TOUCH_RST, LOW);
    io.digitalWrite(XL_ESP32C6_EN, LOW);
    io.digitalWrite(XL_ETHERNET_RST, LOW);
    io.digitalWrite(XL_GPS_WAKEUP, LOW);
    io.digitalWrite(XL_SX1262_RST, LOW);
    // Both rail enables are active LOW, so HIGH is what switches them off.
    io.digitalWrite(XL_SD_POWER_EN, HIGH);
    io.digitalWrite(XL_POWER_EN_5V0, LOW);
    io.digitalWrite(XL_POWER_EN_3V3, HIGH);
}

void powerOff() {
    _setBrightness(0);
    if (p4Panel == P4_PANEL_IPS) {
        ledcDetach(P4_IPS_BL_PIN);
        pinMode(P4_IPS_BL_PIN, OUTPUT);
        digitalWrite(P4_IPS_BL_PIN, LOW);
    }
    _peripherals_power_down();

    delay(200);
    esp_deep_sleep_start();
}

void reboot() {
    // Cut the peripheral rails first and let them drain before the CPU resets.
    // The SD card is the reason: yanking the CPU mid-transaction leaves the
    // card powered and in an undefined state, and because a reset does not
    // power-cycle it, it can come back up refusing to initialize.
    _peripherals_power_down();
    delay(200);
    ESP.restart();
}
