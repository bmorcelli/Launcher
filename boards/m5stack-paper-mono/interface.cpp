#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>

#include <DisplayDrivers.h>

#include "papermono_bsp.h"

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
namespace {
constexpr size_t kPaperMonoControllerFrameBytes = 48000U;

bool submitPaperMonoPackedFrame(const uint8_t *frame, size_t bytes) {
    if (frame == nullptr || bytes != kPaperMonoControllerFrameBytes) return false;
    return PaperMonoBsp::instance().submitMonochromeFrame(frame, kPaperMonoControllerFrameBytes);
}
} // namespace
#endif

#if defined(PAPERMONO_P5_A1B4_ONE_SHOT_FULL_REFRESH) && defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
namespace {
bool paperMonoA1b4FullRefreshLatched = false;

void runPaperMonoA1b4OneShotFullRefresh() {
    if (paperMonoA1b4FullRefreshLatched || !PaperMonoBsp::instance().submittedMonochromeFrameReady()) return;

    Serial.println("[P5-A1B4] staged-frame=valid");

    // Latch before entering the manager so a failure or re-entrant call cannot retry.
    paperMonoA1b4FullRefreshLatched = true;
    Serial.println("[P5-A1B4] full-request=issued");
    const PaperMonoRefreshResult result =
        PaperMonoBsp::instance().requestRefresh(PaperMonoRefreshRequest::Full);
    Serial.printf(
        "[P5-A1B4] result status=%u executed=%u\n",
        static_cast<unsigned>(result.status),
        static_cast<unsigned>(result.executedType)
    );
    Serial.println("[P5-A1B4] one-shot-latch=complete");
}
} // namespace
#endif

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    PaperMonoBsp::instance().begin();
#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
    tft_display::registerPaperMonoFrameSubmitCallback(submitPaperMonoPackedFrame);
#endif
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
#if defined(PAPERMONO_P5_A1B4_ONE_SHOT_FULL_REFRESH) && defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
    runPaperMonoA1b4OneShotFullRefresh();
#endif
}

/***************************************************************************************
** Function name: getBattery()
** Location: display.cpp
** Description:   Delivers the battery value from 0-100; 0 is unavailable
***************************************************************************************/
int getBattery() { return PaperMonoBsp::instance().batteryLevel(); }

/*********************************************************************
** Function: setBrightness
** Location: settings.cpp
** Description: frontlight behavior is deferred until the PaperMono P3/P4 slices
**********************************************************************/
void _setBrightness(uint8_t brightval) { (void)brightval; }

/*********************************************************************
** Function: InputHandler
** Handles touch through the board-local PaperMono service.
**********************************************************************/
void InputHandler(void) {
    static long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        PaperMonoTouchSample sample;
        if (PaperMonoBsp::instance().readTouch(sample) && sample.touched) {
            tm = launcherMillis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            touchPoint.x = sample.x;
            touchPoint.y = sample.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        } else {
            touchPoint.pressed = false;
        }
    }
}

/*********************************************************************
** Function: powerOff
** Location: mykeyboard.cpp
** Description: use the board-local PaperMono PM1 shutdown service
**********************************************************************/
void powerOff() { PaperMonoBsp::instance().powerOff(); }
