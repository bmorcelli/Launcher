#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5Unified.h>
#include <interface.h>

constexpr uint32_t kBtnLongPressMs = 500;

void _setup_gpio() {
    M5.begin(); // Need to test if SDCard inits with the new setup
    M5.BtnA.setDebounceThresh(8);
    M5.BtnB.setDebounceThresh(8);
    M5.BtnA.setHoldThresh(kBtnLongPressMs);
    M5.BtnB.setHoldThresh(kBtnLongPressMs);
}

int getBattery() {
    int percent = 0;
    percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

void _setBrightness(uint8_t brightval) { M5.Display.setBrightness(brightval); }

void InputHandler(void) {
    static unsigned long tm = 0;
    static bool btnALongPressFired = false;
    static bool btnBLongPressFired = false;
    if (millis() - tm < 200 && !LongPress) return;
    vTaskDelay(pdMS_TO_TICKS(50));
    M5.update();

    // Standard launcher 2-button pattern: short click on BtnA -> Next, its
    // long press -> Sel; short click on BtnB -> Prev, its long press -> Esc.
    bool emitNext = false;
    bool emitSel = false;
    bool emitPrev = false;
    bool emitEsc = false;

    auto t = M5.Touch.getDetail();
    if (t.isPressed() || t.isHolding()) {
        tm = millis();
        if (wakeUpScreen()) return;

        touchPoint.x = t.x;
        touchPoint.y = t.y;
        touchPoint.pressed = true;
        Serial.printf("Touched x=%d, y=%d", t.x, t.y);
        touchHeatMap(touchPoint);
    } else touchPoint.pressed = false;

    bool btnAActive = M5.BtnA.isPressed() || M5.BtnA.isHolding();
    bool btnBActive = M5.BtnB.isPressed() || M5.BtnB.isHolding();

    if (M5.BtnA.wasPressed()) btnALongPressFired = false;
    if (btnAActive && !btnALongPressFired && M5.BtnA.pressedFor(kBtnLongPressMs)) {
        btnALongPressFired = true;
        emitSel = true;
    }
    if (M5.BtnA.wasReleased() && !btnALongPressFired) emitNext = true;

    if (M5.BtnB.wasPressed()) btnBLongPressFired = false;
    if (btnBActive && !btnBLongPressFired && M5.BtnB.pressedFor(kBtnLongPressMs)) {
        btnBLongPressFired = true;
        emitEsc = true;
    }
    if (M5.BtnB.wasReleased() && !btnBLongPressFired) emitPrev = true;

    AnyKeyPress = btnAActive || btnBActive || emitNext || emitSel || emitPrev || emitEsc;
    if (!AnyKeyPress) return;

    if ((btnAActive || btnBActive) && wakeUpScreen()) return;

    if (emitNext) NextPress = true;
    if (emitSel) SelPress = true;
    if (emitPrev) PrevPress = true;
    if (emitEsc) EscPress = true;
}

void powerOff() { M5.Power.powerOff(); }
