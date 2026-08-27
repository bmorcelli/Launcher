#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>

#include <DisplayDrivers.h>

#include "papermono_bsp.h"

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
namespace {
constexpr size_t kPaperMonoControllerFrameBytes = 48000U;

class PaperMonoInteractiveRefreshScheduler {
public:
    void noteFrameStaged() {
        uint32_t generation = 0;
        portENTER_CRITICAL(&mutex_);
        ++frameGeneration_;
        generation = frameGeneration_;
        portEXIT_CRITICAL(&mutex_);
        Serial.printf("[P5-B1] staged generation=%lu\n", static_cast<unsigned long>(generation));
    }

    void armInteraction() {
        uint32_t generation = 0;
        bool armed = false;
        portENTER_CRITICAL(&mutex_);
        if (!interactionPending_) {
            interactionPending_ = true;
            armedGeneration_ = frameGeneration_;
            generation = armedGeneration_;
            armed = true;
        }
        portEXIT_CRITICAL(&mutex_);

        if (armed) {
            Serial.printf(
                "[P5-B1] interaction=armed generation=%lu\n", static_cast<unsigned long>(generation)
            );
        }
    }

    void service() {
        uint32_t generation = 0;
        bool bootRefresh = false;
        bool interactionRefresh = false;

        portENTER_CRITICAL(&mutex_);
        const bool bootEligible = bootRefreshPending_ && frameGeneration_ != 0U;
        const bool interactionEligible = interactionPending_ && frameGeneration_ > armedGeneration_;
        if (bootEligible || interactionEligible) {
            generation = frameGeneration_;
            bootRefresh = bootEligible;
            interactionRefresh = interactionEligible;
            bootRefreshPending_ = false;
            if (interactionEligible) interactionPending_ = false;
            lastRefreshedGeneration_ = generation;
        }
        portEXIT_CRITICAL(&mutex_);

        if (!bootRefresh && !interactionRefresh) return;

        if (bootRefresh) {
            Serial.printf(
                "[P5-B1] boot-full=issued generation=%lu\n", static_cast<unsigned long>(generation)
            );
        } else {
            Serial.printf(
                "[P5-B1] interaction-full=issued generation=%lu\n", static_cast<unsigned long>(generation)
            );
        }

        const PaperMonoRefreshResult result =
            PaperMonoBsp::instance().requestRefresh(PaperMonoRefreshRequest::Full);
        Serial.printf(
            "[P5-B1] refresh-result status=%u executed=%u generation=%lu\n",
            static_cast<unsigned>(result.status),
            static_cast<unsigned>(result.executedType),
            static_cast<unsigned long>(generation)
        );

        if (interactionRefresh) { Serial.println("[P5-B1] interaction=disarmed"); }
    }

private:
    portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
    uint32_t frameGeneration_ = 0;
    uint32_t armedGeneration_ = 0;
    uint32_t lastRefreshedGeneration_ = 0;
    bool bootRefreshPending_ = true;
    bool interactionPending_ = false;
};

PaperMonoInteractiveRefreshScheduler paperMonoRefreshScheduler;

bool submitPaperMonoPackedFrame(const uint8_t *frame, size_t bytes) {
    if (frame == nullptr || bytes != kPaperMonoControllerFrameBytes) return false;
    const bool staged = PaperMonoBsp::instance().submitMonochromeFrame(frame, kPaperMonoControllerFrameBytes);
    if (staged) paperMonoRefreshScheduler.noteFrameStaged();
    return staged;
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
#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
    paperMonoRefreshScheduler.service();
#endif
}

void _post_main_menu_display() {
#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
    paperMonoRefreshScheduler.service();
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
    static bool touchWasDown = false;
    if (launcherMillis() - tm > 200 || LongPress) {
        PaperMonoTouchSample sample;
        if (PaperMonoBsp::instance().readTouch(sample) && sample.touched) {
            tm = launcherMillis();
            if (touchWasDown) {
                touchPoint.pressed = false;
                return;
            }
            touchWasDown = true;
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

#if defined(PAPERMONO_PRODUCTION_DISPLAY_BACKEND)
            paperMonoRefreshScheduler.armInteraction();
#endif
            touchPoint.x = sample.x;
            touchPoint.y = sample.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        } else {
            touchWasDown = false;
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
