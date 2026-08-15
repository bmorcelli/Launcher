#include "utils.h"
#include "display.h"
#include "esp_wifi.h"
#include "settings.h"
#include <SD.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <globals.h>
/*********************************************************************
** Function: touchHeatMap
** Touchscreen Mapping, include this function after reading the touchPoint
**********************************************************************/
void touchHeatMap(struct LTouchPoint t) {
    int third_x = tftWidth / 3;
    int third_y = tftHeight / 3;

#if 1 // defined(DONT_USE_INPUT_TASK)
    if (t.x > third_x * 0 && t.x < third_x * 1 && t.y > tftHeight - LH * _fm * 2) PrevPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && t.y > tftHeight - LH * _fm * 2) SelPress = true;
    if (t.x > third_x * 2 && t.x < third_x * 3 && t.y > tftHeight - LH * _fm * 2) NextPress = true;
    if (t.x > third_x * 0 && t.x < third_x * 1 && t.y < LH * _fm * 4) EscPress = true;

    /*
                        Touch area Map
                ________________________________ 0
                |_Esc_|_______________________|
                |_____________________________|
                |_____________________________|
                |_____________________________|
                |_____________________________|
                |_____________________________|
                |__Prev___|___Sel___|__Next___| 30 pixel touch area where the touchFooter is drawn
                0         L third_x |         |
                                    Lthird_x*2|
                                              Lthird_x*3
    */

#else
    if (t.x > third_x * 0 && t.x < third_x * 1 && t.y > third_y) PrevPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && ((t.y > third_y && t.y < third_y * 2) || t.y > tftHeight))
        SelPress = true;
    if (t.x > third_x * 2 && t.x < third_x * 3) NextPress = true;
    if (t.x > third_x * 0 && t.x < third_x * 1 && t.y < third_y) EscPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && t.y < third_y) UpPress = true;
    if (t.x > third_x * 1 && t.x < third_x * 2 && t.y > third_y * 2 && t.y < third_y * 3) DownPress = true;
    /*
                        Touch area Map
                ________________________________ 0
                |   Esc   |   UP    |         |
                |_________|_________|         |_> third_y
                |         |   Sel   |         |
                |         |_________|  Next   |_> third_y*2
                |  Prev   |  Down   |         |
                |_________|_________|_________|_> third_y*3
                |__Prev___|___Sel___|__Next___| 20 pixel touch area where the touchFooter is drawn
                0         L third_x |         |
                                    Lthird_x*2|
                                              Lthird_x*3
    */
#endif
}

// Prefer PSRAM (isolates ArduinoJson churn from the internal heap), fall back
// to internal RAM on boards without PSRAM (e.g. m5stack-cardputer).
class LauncherJsonAllocator : public ArduinoJson::Allocator {
public:
    void *allocate(size_t size) override {
        void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        return p;
    }

    void deallocate(void *ptr) override { heap_caps_free(ptr); }

    void *reallocate(void *ptr, size_t new_size) override {
        // heap_caps_realloc leaves the original block untouched when it returns
        // NULL, so retrying with a different capability is safe.
        void *p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        return p;
    }
};

ArduinoJson::Allocator *launcherJsonAllocator() {
    static LauncherJsonAllocator instance;
    return &instance;
}

void buildFirmwareListFilter(JsonDocument &filter) {
    filter["total"] = true;
    filter["page"] = true;
    filter["page_size"] = true;
    // The first element of a filter array is the template applied to every
    // element of the input array.
    JsonObject item = filter["items"].add<JsonObject>();
    item["fid"] = true;
    item["file"] = true;
    item["name"] = true;
    item["version"] = true;
    item["author"] = true;
    item["star"] = true;
}

bool releaseHeapObjectsAndReboot(void) {
    static volatile bool rebootInProgress = false;
    if (rebootInProgress) {
        reboot();
        return true;
    }
    rebootInProgress = true;

    if (xHandle != nullptr) vTaskSuspend(xHandle);
    esp_wifi_stop();
    eraseNamespace("nvs.net80211");

    doc.clear();
    doc.shrinkToFit();

    JsonArray favorites = settings["favorite"].as<JsonArray>();
    if (!favorites.isNull()) favorites.clear();
    favorite = JsonArray();

    settings.clear();
    settings.shrinkToFit();

    SDM.end();
    options.clear();

    fileToCopy = "";
    ssid = "";
    pwd = "";
    wui_usr = "";
    wui_pwd = "";
    dwn_path = "";
    lastInstalledApp = "";
    FREE_TFT
    reboot();
    return true;
}
