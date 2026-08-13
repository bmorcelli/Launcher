
#include "settings.h"
#include "display.h"
#include "esp_mac.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "nvs.h"
#include "nvs_handle.hpp"
#include "onlineLauncher.h"
#include "partitioner.h"
#include "powerSave.h"
#include "sd_functions.h"
#include "utils.h"
#include "wifi_crypto.h"
#include <FS.h>
#include <SD.h>
#include <cstdio>
#include <cstdlib>
#include <globals.h>
#include <memory>
#if !defined(SDM_SD)
#include <SD_MMC.h>
#endif
#ifdef USE_CARDKB2
#include <cardkb2.h>
#endif

// forward declaration
void defaultValues();

namespace {
// Avoids re-erasing/rewriting the whole l_wifi NVS namespace on every
// saveConfigs() call when the wifi list itself hasn't changed.
bool wifiListDirty = true;
uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    while (length--) {
        crc ^= *data++;
        for (int i = 0; i < 8; ++i) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

String makeWifiKey(char prefix, uint32_t crc) {
    char key[11] = {0};
    std::snprintf(key, sizeof(key), "%c_%08X", prefix, crc);
    return String(key);
}

std::unique_ptr<nvs::NVSHandle> openNamespace(const char *ns, nvs_open_mode_t mode, esp_err_t &err) {
    auto handle = nvs::open_nvs_handle(ns, mode, &err);
    if (err != ESP_OK) {
        log_i("openNamespace(%s) failed: %d", ns, err);
        return nullptr;
    }
    return handle;
}

JsonArray ensureWifiListInternal() {
    JsonObject setting = ensureSettingsRoot();
    if (setting.isNull()) return JsonArray();

    JsonArray wifiList = setting["wifi"].as<JsonArray>();
    if (wifiList.isNull()) { wifiList = setting.createNestedArray("wifi"); }
    if (wifiList.isNull()) { log_e("ensureWifiList: failed to create wifi list"); }
    return wifiList;
}

JsonObject ensureKeyBindingObjectInternal() {
    JsonObject setting = ensureSettingsRoot();
    if (setting.isNull()) return JsonObject();

    JsonObject bindings = setting["key_binding"].as<JsonObject>();
    if (bindings.isNull()) { bindings = setting.createNestedObject("key_binding"); }
    if (bindings.isNull()) { log_e("ensureKeyBindingObject: failed to create key_binding object"); }
    return bindings;
}

bool ensureStringKey(nvs::NVSHandle &handle, const char *key, const char *value) {
    char buffer[64] = {0};
    esp_err_t err = handle.get_string(key, buffer, sizeof(buffer));
    if (err == ESP_OK) return false;
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        launcherConsolePrintf("ensureStringKey(%s) read failed: %d", key, err);
        return false;
    }

    err = handle.set_string(key, value);
    if (err != ESP_OK) { launcherConsolePrintf("ensureStringKey(%s) write failed: %d", key, err); }
    return err == ESP_OK;
}

bool ensureU8Key(nvs::NVSHandle &handle, const char *key, uint8_t value) {
    uint8_t current = 0;
    esp_err_t err = handle.get_item(key, current);
    if (err == ESP_OK) return false;
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        launcherConsolePrintf("ensureU8Key(%s) read failed: %d", key, err);
        return false;
    }

    err = handle.set_item(key, value);
    if (err != ESP_OK) { launcherConsolePrintf("ensureU8Key(%s) write failed: %d", key, err); }
    return err == ESP_OK;
}

bool eraseNamespace(const char *ns) {
    esp_err_t err = ESP_OK;
    auto handle = openNamespace(ns, NVS_READWRITE, err);
    if (!handle) return false;
    err = handle->erase_all();
    if (err != ESP_OK) return false;
    err = handle->commit();
    return err == ESP_OK;
}

bool backupConfigFileIfPresent() {
    if (!setupSdCard()) return true;
    String backupPath = String(CONFIG_FILE) + ".old";
    if (SDM.exists(backupPath)) SDM.remove(backupPath);
    if (!SDM.exists(CONFIG_FILE)) return true;
    return SDM.rename(CONFIG_FILE, backupPath);
}

void factoryReset() {
    bool confirmed = false;
    options = {
        {"Reset Configs/Wifi", [&]() { confirmed = true; } },
        {"Cancel",             [&]() { confirmed = false; }},
    };
    loopOptions(options);
    if (!confirmed) return;
    eraseNamespace("l_wifi");
    eraseNamespace("launcher");
    backupConfigFileIfPresent();
    favorite = JsonArray();
    settings.clear();
    wifiListDirty = true;
    defaultValues();
    saveConfigs();
}
} // namespace

JsonObject ensureSettingsRoot() {
    JsonArray settingsArray = settings.as<JsonArray>();
    if (settingsArray.isNull()) {
        favorite = JsonArray();
        settings.clear();
        settingsArray = settings.to<JsonArray>();
    }
    if (settingsArray.isNull()) {
        log_e("ensureSettingsRoot: unable to prepare settings array");
        return JsonObject();
    }

    JsonObject setting;
    if (settingsArray.size() > 0 && settingsArray[0].is<JsonObject>()) {
        setting = settingsArray[0].as<JsonObject>();
    } else {
        favorite = JsonArray();
        settingsArray.clear();
        setting = settingsArray.add<JsonObject>();
    }

    if (setting.isNull()) { log_e("ensureSettingsRoot: failed to create root object"); }
    return setting;
}

bool getWifiCredential(const String &searchSsid, String &outPwd) {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    for (JsonObject wifiEntry : wifiList) {
        if (wifiEntry["ssid"].as<String>() == searchSsid) {
            outPwd = wifiEntry["pwd"].as<String>();
            return true;
        }
    }

    return false;
}

bool ensureM5StackUiFlowNVSDefaults() {
#if defined(M5STACK)
    esp_err_t err = ESP_OK;
    auto nvsHandle = openNamespace("uiflow", NVS_READWRITE, err);
    if (!nvsHandle) return false;

    bool changed = false;
    // https://github.com/m5stack/uiflow-micropython/blob/master/m5stack/partition_nvs.csv

    changed |= ensureStringKey(*nvsHandle, "server", "uiflow2.m5stack.com");
    changed |= ensureStringKey(*nvsHandle, "net_mode", "WIFI");
    changed |= ensureStringKey(*nvsHandle, "protocol", "DHCP");
    changed |= ensureStringKey(*nvsHandle, "ip_addr", "");
    changed |= ensureStringKey(*nvsHandle, "netmask", "");
    changed |= ensureStringKey(*nvsHandle, "gateway", "");
    changed |= ensureStringKey(*nvsHandle, "dns", "8.8.8.8");
    changed |= ensureStringKey(*nvsHandle, "ssid0", "");
    changed |= ensureStringKey(*nvsHandle, "pswd0", "");
    changed |= ensureStringKey(*nvsHandle, "ssid1", "");
    changed |= ensureStringKey(*nvsHandle, "pswd1", "");
    changed |= ensureStringKey(*nvsHandle, "ssid2", "");
    changed |= ensureStringKey(*nvsHandle, "pswd2", "");
    changed |= ensureStringKey(*nvsHandle, "sntp0", "ntp.aliyun.com");
    changed |= ensureStringKey(*nvsHandle, "sntp1", "jp.pool.ntp.org");
    changed |= ensureStringKey(*nvsHandle, "sntp2", "pool.ntp.org");
    changed |= ensureStringKey(*nvsHandle, "tz", "GMT0");
    changed |= ensureU8Key(*nvsHandle, "boot_option", 1);

    if (changed) {
        err = nvsHandle->commit();
        if (err != ESP_OK) {
            launcherConsolePrintf("ensureM5StackUiFlowNVSDefaults: commit failed: %d", err);
            return false;
        }
        launcherConsolePrintln("ensureM5StackUiFlowNVSDefaults: default UiFlow keys created");
    }

    return true;
#else
    return true;
#endif
}

bool setWifiCredential(const String &ssidValue, const String &passwordValue, bool persist) {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    JsonObject target;
    for (JsonObject wifiEntry : wifiList) {
        if (wifiEntry["ssid"].as<String>() == ssidValue) {
            target = wifiEntry;
            break;
        }
    }
    if (target.isNull()) { target = wifiList.createNestedObject(); }
    if (target.isNull()) {
        log_e("setWifiCredential: failed to allocate entry");
        return false;
    }

    target["ssid"] = ssidValue;
    target["pwd"] = passwordValue;

    wifiListDirty = true;
    if (persist) { saveConfigs(); }
    return true;
}

bool removeWifiCredential(const String &ssidValue) {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    for (size_t i = 0; i < wifiList.size(); i++) {
        if (wifiList[i]["ssid"].as<String>() == ssidValue) {
            wifiList.remove(i);
            wifiListDirty = true;
            saveConfigs();
            return true;
        }
    }
    return false;
}

bool clearWifiCredentials() {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    while (wifiList.size() > 0) wifiList.remove(0);
    wifiListDirty = true;
    saveConfigs();
    return true;
}

bool getKeyBinding(const String &key, String &outPath) {
    JsonObject bindings = ensureKeyBindingObjectInternal();
    if (bindings.isNull()) return false;
    if (!bindings[key].is<String>()) return false;
    outPath = bindings[key].as<String>();
    return true;
}

bool setKeyBinding(const String &key, const String &path, bool persist) {
    JsonObject bindings = ensureKeyBindingObjectInternal();
    if (bindings.isNull()) return false;

    bindings[key] = path;
    if (persist) saveConfigs();
    return true;
}

bool removeKeyBinding(const String &key, bool persist) {
    JsonObject bindings = ensureKeyBindingObjectInternal();
    if (bindings.isNull()) return false;
    if (!bindings[key].is<String>()) return false;

    bindings.remove(key);
    if (persist) saveConfigs();
    return true;
}

bool clearKeyBindings() {
    JsonObject setting = ensureSettingsRoot();
    if (setting.isNull()) return false;

    setting.remove("key_binding");
    saveConfigs();
    return true;
}

#if defined(HAS_KEYBOARD)
static void manageKeyBindings() {
    int idx = 0;
    returnToMenu = false;
    while (idx >= 0 && !returnToMenu) {
        JsonObject bindings = ensureKeyBindingObjectInternal();
        std::vector<Option> opts;
        for (JsonPair kv : bindings) {
            String key = kv.key().c_str();
            opts.push_back({String("'") + key + "': Remove", [key]() { removeKeyBinding(key); }});
        }
        opts.push_back({"Reset All", [=]() { clearKeyBindings(); }});
        opts.push_back({"Back to Menu", [&]() { returnToMenu = true; }});
        idx = loopOptions(opts);
    }
}
#endif

void settings_menu() {
    int idx = 0;
    returnToMenu = false;
    while (idx >= 0 && !returnToMenu) {
        options = {
#ifndef E_PAPER_DISPLAY
            {"Charge Mode",
                                   [=]() {
                 chargeMode();
                 returnToMenu = true;
             }                                                 },
#endif
            {"Brightness",
                                   [=]() {
                 setBrightnessMenu();
                 saveConfigs();
             }                                                 },
            {"Dim time",
                                   [=]() {
                 setdimmerSet();
                 saveConfigs();
             }                                                 },
#if !defined(E_PAPER_DISPLAY)
            {"UI Color",
                                   [=]() {
                 setUiColor();
                 saveConfigs();
             }                                                 },
#endif
#if !defined(LYLYGO_TDECK_PRO)
            {"Orientation", [=]() {
                 gsetRotation(true);
                 saveConfigs();
             }}
#endif
        };
        if (sdcardMounted) {
            options.push_back({onlyBins ? "[ ] See All Files" : "[x] See All Files", [=]() {
                                   onlyBins = !onlyBins;
                                   saveConfigs();
                               }});
            options.push_back({noDotFiles ? "[ ] Show Dotfiles" : "[x] Show Dotfiles", [=]() {
                                   noDotFiles = !noDotFiles;
                                   saveConfigs();
                               }});
            options.push_back({autoBackup ? "[x] Auto Backup" : "[ ] Auto Backup", [=]() {
                                   autoBackup = !autoBackup;
                                   saveConfigs();
                               }});
        }

        options.push_back({bootToApp ? "[ ] Boot to Launcher" : "[x] Boot to Launcher", [=]() {
                               bootToApp = !bootToApp;
                               saveConfigs();
                           }});
        options.push_back({askSpiffs ? "[x] Ask to copy SPIFFS" : "[ ] Ask to copy SPIFFS", [=]() {
                               askSpiffs = !askSpiffs;
                               saveConfigs();
                           }});
        options.push_back({"Partition Manager", [=]() { partList(); }});
#if defined(HAS_KEYBOARD)
        options.push_back({"Manage shortcuts", [=]() { manageKeyBindings(); }});
#endif

        if (dev_mode) options.push_back({"Boot Animation", [=]() { initDisplayLoop(); }});
        if (dev_mode)
            options.push_back({"Deactivate Dev", [=]() {
                                   dev_mode = false;
                                   saveConfigs();
                               }});
#if defined(HAS_RESISTIVE_TOUCH)
        options.push_back({"Calibrate Touch", calibrateTouch});
#endif
#if defined(USE_CARDKB2) && defined(CARDKB2_SDA) && defined(CARDKB2_SCL)
        options.push_back({"Start CardKb", [=]() { cardkb2_setup(CARDKB2_SDA, CARDKB2_SCL); }});
#endif
        // Only worth offering when the co-processor was latched off: clearing the
        // guard re-arms the bring-up, which only runs at boot, so reboot with it.
        if (!hostedWifiAvailable) {
            options.push_back({"Retry WiFi Module", [=]() {
                                   launcherWifiHostedResetGuard();
                                   releaseHeapObjectsAndReboot();
                               }});
        }
        if (dev_mode) options.push_back({"Reset Configs/Wifi", factoryReset});
        options.push_back({"Restart", [=]() { return (void)releaseHeapObjectsAndReboot(); }});
        options.push_back({"Turn-off", [=]() { powerOff(); }});

        options.push_back({"Main Menu", [=]() { returnToMenu = true; }});
        idx = loopOptions(options);
    }
    tft->drawPixel(0, 0, 0);
    tft->fillScreen(BGCOLOR);
}

// This function comes from interface.h
void _setBrightness(uint8_t brightval) {
#ifdef TFT_BL
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
    } else {
        const uint8_t PWM_MIN = 85;
        const uint8_t PWM_MAX = 255;
        float linear = (float)brightval / 100.0;
        uint8_t value = PWM_MIN + round(pow(linear, 2.2) * (PWM_MAX - PWM_MIN));
        analogWrite(TFT_BL, value);
    }
#endif
}

/*********************************************************************
**  Function: setBrightness
**  save brightness value into EEPROM
**********************************************************************/
void setBrightness(int brightval, bool save) {
    if (brightval > 100) brightval = 100;
    _setBrightness(brightval);
    if (save) { bright = brightval; }
}

/*********************************************************************
**  Function: getBrightness
**  save brightness value into EEPROM
**********************************************************************/
void getBrightness() {
    if (bright > 100) {
        bright = 100;
        _setBrightness(bright);
        setBrightness(100);
    }
    _setBrightness(bright);
}

/*********************************************************************
**  Function: gsetRotation
**  get onlyBins from EEPROM
**********************************************************************/
#define DRV 1
int gsetRotation(bool set) {
    int result = ROTATION;

    if (rotation > 3) {
        set = true;
        result = ROTATION;
    } else result = rotation;

    if (set) {
        options = {
            {"Default",                                              [&]() { result = ROTATION; }          },
#if TFT_WIDTH >= 200 && TFT_HEIGHT >= 200
            {String("Portrait " + String(DRV == 1 ? 0 : 1)).c_str(), [&]() { result = (DRV == 1 ? 0 : 1); }},
#endif
            {String("Landscape " + String(DRV)).c_str(),             [&]() { result = DRV; }               },
#if TFT_WIDTH >= 200 && TFT_HEIGHT >= 200
            {String("Portrait " + String(DRV == 1 ? 2 : 3)).c_str(), [&]() { result = (DRV == 1 ? 2 : 3); }},
#endif
            {String("Landscape " + String(DRV + 2)).c_str(),         [&]() { result = DRV + 2; }           }
        };
        loopOptions(options);
        rotation = result;

        if (rotation & 0b1) {
#if defined(HAS_TOUCH)
            tftHeight = TFT_WIDTH - (_fm * LH + 4);
#else
            tftHeight = TFT_WIDTH;
#endif
            tftWidth = TFT_HEIGHT;
        } else {
#if defined(HAS_TOUCH)
            tftHeight = TFT_HEIGHT - (_fm * LH + 4);
#else
            tftHeight = TFT_HEIGHT;
#endif
            tftWidth = TFT_WIDTH;
        }

        tft->setRotation(result);
        tft->fillScreen(BGCOLOR);
    }
    return result;
}
/*********************************************************************
**  Function: setBrightnessMenu
**  Handles Menu to set brightness
**********************************************************************/
void setBrightnessMenu() {
    options = {
        {"100%", [=]() { setBrightness(100); }},
        {"75 %", [=]() { setBrightness(75); } },
        {"50 %", [=]() { setBrightness(50); } },
        {"25 %", [=]() { setBrightness(25); } },
        // {" 0 %", [=]() { setBrightness(1); }  },
    };
    loopOptions(options, true);
}
/*********************************************************************
**  Function: setUiColor
**  Change Ui Color scheme
**********************************************************************/
void setUiColor() {
    options = {
        {"Default",
         [&]() {
             FGCOLOR = 0x07E0;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xF800;
             odd_color = 0x30c5;
             even_color = 0x32e5;
         }                 },
        {"Red",
         [&]() {
             FGCOLOR = 0xF800;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xE3E0;
             odd_color = 0xFBC0;
             even_color = 0xAAC0;
         }                 },
        {"Blue",
         [&]() {
             FGCOLOR = 0x94BF;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xd81f;
             odd_color = 0xd69f;
             even_color = 0x079F;
         }                 },
        {"Yellow",
         [&]() {
             FGCOLOR = 0xFFE0;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xFB80;
             odd_color = 0x9480;
             even_color = 0xbae0;
         }                 },
        {"Purple",
         [&]() {
             FGCOLOR = 0xe01f;
             BGCOLOR = 0x0000;
             ALCOLOR = 0xF800;
             odd_color = 0xf57f;
             even_color = 0x89d3;
         }                 },
        {"White",
         [&]() {
             FGCOLOR = 0xFFFF;
             BGCOLOR = 0x0000;
             ALCOLOR = 0x6b6d;
             odd_color = 0x630C;
             even_color = 0x8410;
         }                 },
        {"Black",   [&]() {
             FGCOLOR = 0x0000;
             BGCOLOR = 0xFFFF;
             ALCOLOR = 0x6b6d;
             odd_color = 0x8c71;
             even_color = 0xb596;
         }},
    };
    loopOptions(options);
    displayRedStripe("Saving...");
}
/*********************************************************************
**  Function: setdimmerSet
**  set dimmerSet time
**********************************************************************/
void setdimmerSet() {
    int time = 20;
    options = {
        {"10s",     [&]() { time = 10; }},
        {"15s",     [&]() { time = 15; }},
        {"30s",     [&]() { time = 30; }},
        {"45s",     [&]() { time = 45; }},
        {"60s",     [&]() { time = 60; }},
        {"Disable", [&]() { time = 0; } },
    };

    loopOptions(options);
    dimmerSet = time;
}

/*********************************************************************
**  Function: chargeMode
**  Enter in Charging mode
**********************************************************************/
void chargeMode() {
#ifndef CONFIG_IDF_TARGET_ESP32P4
    setCpuFrequencyMhz(80);
#endif
    setBrightness(25, false);
    vTaskDelay(pdTICKS_TO_MS(500));
    tft->fillScreen(BGCOLOR);
    unsigned long tmp = 0;
    while (!check(SelPress)) {
        if (launcherMillis() - tmp > 5000) {
            // The whole point of this mode is a device that goes dark on the charger,
            // so this repaint must not restart the screen-off timer: it comes round
            // faster than the shortest timeout and would hold the backlight forever.
            displayRedStripe(String(getBattery()) + " %", getComplementaryColor(BGCOLOR), ALCOLOR, false);
            tmp = launcherMillis();
        }
    }
#ifndef CONFIG_IDF_TARGET_ESP32P4
    setCpuFrequencyMhz(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
#endif
    setBrightness(bright, false);
}
String get_efuse_mac_as_string() {
    uint8_t mac[6] = {0};
    String str = "";
    esp_efuse_mac_get_default(mac);
    for (int i = 0; i < 6; i++) {
        if (i > 0) str += ":";
        str += String(mac[i], 16);
    }
    return str;
}

bool saveIntoNVS() {
    esp_err_t err = ESP_OK;
    auto nvsHandle = openNamespace("launcher", NVS_READWRITE, err);
    if (!nvsHandle) return false;

    err |= nvsHandle->set_item("dimtime", dimmerSet);
    err |= nvsHandle->set_item("bright", bright);
    err |= nvsHandle->set_item("onlyBins", onlyBins);
    err |= nvsHandle->set_item("bootToApp", bootToApp);
    err |= nvsHandle->set_item("noDotFiles", noDotFiles);
    err |= nvsHandle->set_item("autoBackup", autoBackup);
    err |= nvsHandle->set_item("askSpiffs", askSpiffs);
    err |= nvsHandle->set_item("rotation", rotation);
    err |= nvsHandle->set_item("FGCOLOR", FGCOLOR);
    err |= nvsHandle->set_item("BGCOLOR", BGCOLOR);
    err |= nvsHandle->set_item("ALCOLOR", ALCOLOR);
    err |= nvsHandle->set_item("odd_color", odd_color);
    err |= nvsHandle->set_item("even_color", even_color);
    err |= nvsHandle->set_item("dev_mode", dev_mode);
    err |= nvsHandle->set_string("wui_usr", wui_usr.c_str());
    err |= nvsHandle->set_string("wui_pwd", wui_pwd.c_str());
    err |= nvsHandle->set_string("dwn_path", dwn_path.c_str());
    err |= nvsHandle->set_string("last_app", lastInstalledApp.c_str());
#if defined(HEADLESS)
    // SD Pins
    err |= nvsHandle->set_item("miso", _miso);
    err |= nvsHandle->set_item("mosi", _mosi);
    err |= nvsHandle->set_item("sck", _sck);
    err |= nvsHandle->set_item("cs", _cs);
#endif
    if (err != ESP_OK) {
        launcherConsolePrintf("Failed to store settings in NVS: %d", err);
    } else {
        launcherConsolePrintln("Settings stored in NVS successfully");
    }

    nvsHandle->commit();
    if (!saveWifiIntoNVS()) { launcherConsolePrintln("saveIntoNVS: failed to store WiFi list"); }
    return true;
}

bool saveSessionToken(const String &token) {
    esp_err_t err = ESP_OK;
    auto nvsHandle = openNamespace("launcher", NVS_READWRITE, err);
    if (!nvsHandle) return false;

    if (token.isEmpty()) {
        err = nvsHandle->erase_item("token");
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else {
        err = nvsHandle->set_string("token", token.c_str());
    }

    if (err == ESP_OK) { err = nvsHandle->commit(); }
    return err == ESP_OK;
}

bool saveWifiIntoNVS() {
    if (!wifiListDirty) return true;

    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;

    esp_err_t err = ESP_OK;
    auto nvsHandle = openNamespace("l_wifi", NVS_READWRITE, err);
    if (!nvsHandle) return false;

    err = nvsHandle->erase_all();
    if (err != ESP_OK) { log_i("saveWifiIntoNVS: failed to clear WiFi namespace: %d", err); }

    for (JsonObject wifiObj : wifiList) {
        String ssid = wifiObj["ssid"].as<String>();
        if (ssid.isEmpty()) continue;
        String encPwd = wifiPwdEncrypt(wifiObj["pwd"].as<String>());
        uint32_t crc = crc32(reinterpret_cast<const uint8_t *>(ssid.c_str()), ssid.length());
        String ssidKey = makeWifiKey('s', crc);
        String pwdKey = makeWifiKey('p', crc);
        String secKey = makeWifiKey('b', crc);

        esp_err_t ssidErr = nvsHandle->set_string(ssidKey.c_str(), ssid.c_str());
        esp_err_t pwdErr = nvsHandle->set_string(pwdKey.c_str(), encPwd.c_str());
        esp_err_t secErr = nvsHandle->set_item(secKey.c_str(), (uint8_t)1);
        if (ssidErr != ESP_OK || pwdErr != ESP_OK || secErr != ESP_OK) {
            log_i(
                "saveWifiIntoNVS: failed storing %s (ssid=%d pwd=%d sec=%d)",
                ssid.c_str(),
                ssidErr,
                pwdErr,
                secErr
            );
        }
    }

    nvsHandle->commit();
    wifiListDirty = false;
    return true;
}

String loadSessionToken() {
    esp_err_t err = ESP_OK;
    auto nvsHandle = openNamespace("launcher", NVS_READONLY, err);
    if (!nvsHandle) return "";

    char buffer[65] = {0};
    err = nvsHandle->get_string("token", buffer, sizeof(buffer));
    if (err != ESP_OK) return "";

    return String(buffer);
}

void defaultValues() {
    // rotation = ROTATION;
#ifdef DIMMER_SETUP
    dimmerSet = DIMMER_SETUP;
#else
    dimmerSet = 20;
#endif
    bright = 100;
    onlyBins = true;
    bootToApp = true;
    noDotFiles = true;
    askSpiffs = true;
    autoBackup = true;
#if defined(E_PAPER_DISPLAY)
    FGCOLOR = 0x0000;
    BGCOLOR = 0xFFFF;
    ALCOLOR = 0x8888;
    odd_color = 0x5555;
    even_color = 0x2222;
#else
    FGCOLOR = 0x07E0;
    BGCOLOR = 0x0000;
    ALCOLOR = 0xF800;
    odd_color = 0x30c5;
    even_color = 0x32e5;
#endif
    dev_mode = false;
    wui_usr = "admin";
    wui_pwd = "launcher";
    dwn_path = "/downloads/";
#if defined(HEADLESS)
    // SD Pins
    _miso = 0;
    _mosi = 0;
    _sck = 0;
    _cs = 0;
#endif
    saveIntoNVS();
}
bool getFromNVS() {
    esp_err_t err;
    std::unique_ptr<nvs::NVSHandle> nvsHandle = nvs::open_nvs_handle("launcher", NVS_READONLY, &err);
    if (err != ESP_OK) {
        // If NVS read fails, set default values
        log_i("Failed to retrieve settings from NVS: %d\nUsing Default values", err);
        defaultValues();
        return false;
    }
    // Get settings from NVS
    if (err != ESP_OK) {
        log_i("Failed to open NVS handle: %d", err);
        return false;
    }
    err = nvsHandle->get_item("dimtime", dimmerSet);
    err |= nvsHandle->get_item("bright", bright);
    err |= nvsHandle->get_item("onlyBins", onlyBins);
    err |= nvsHandle->get_item("bootToApp", bootToApp);
    err |= nvsHandle->get_item("noDotFiles", noDotFiles);
    err |= nvsHandle->get_item("autoBackup", autoBackup);
    err |= nvsHandle->get_item("askSpiffs", askSpiffs);
    err |= nvsHandle->get_item("rotation", rotation);
    err |= nvsHandle->get_item("FGCOLOR", FGCOLOR);
    err |= nvsHandle->get_item("BGCOLOR", BGCOLOR);
    err |= nvsHandle->get_item("ALCOLOR", ALCOLOR);
    err |= nvsHandle->get_item("odd_color", odd_color);
    err |= nvsHandle->get_item("even_color", even_color);
    err |= nvsHandle->get_item("dev_mode", dev_mode);
#if defined(HEADLESS)
    // SD Pins
    err |= nvsHandle->get_item("miso", _miso);
    err |= nvsHandle->get_item("mosi", _mosi);
    err |= nvsHandle->get_item("sck", _sck);
    err |= nvsHandle->get_item("cs", _cs);
#endif
    char buffer[64];
    err |= nvsHandle->get_string("wui_usr", buffer, sizeof(buffer));
    wui_usr = String(buffer);
    err |= nvsHandle->get_string("wui_pwd", buffer, sizeof(buffer));
    wui_pwd = String(buffer);
    err |= nvsHandle->get_string("dwn_path", buffer, sizeof(buffer));
    dwn_path = String(buffer);
    char appBuffer[128] = {0};
    esp_err_t lastAppErr = nvsHandle->get_string("last_app", appBuffer, sizeof(appBuffer));
    if (lastAppErr == ESP_OK) lastInstalledApp = String(appBuffer);
    else if (lastAppErr == ESP_ERR_NVS_NOT_FOUND) lastInstalledApp = "";
    else err |= lastAppErr;
    // ESP_ERR_NVS_NOT_FOUND is expected after a firmware update adds new settings keys
    // that haven't been written yet. Keep values at their defaults instead of wiping everything.
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        log_i("Failed to retrieve settings from NVS: %d\nUsing Default values", err);
        defaultValues();
        return false;
    }

    return true;
}
bool getWifiFromNVS() {
    JsonArray wifiList = ensureWifiListInternal();
    if (wifiList.isNull()) return false;
    wifiList.clear();

    launcherConsolePrintln("NVS: Finding keys in NVS...");
    nvs_handle_t rawHandle;
    esp_err_t err = nvs_open("l_wifi", NVS_READONLY, &rawHandle);
    if (err != ESP_OK) {
        launcherConsolePrintf("Error opening l_wifi: %s\n", esp_err_to_name(err));
        return false;
    }

    nvs_iterator_t it = nullptr;
    err = nvs_entry_find("nvs", "l_wifi", NVS_TYPE_ANY, &it);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(rawHandle);
        return true;
    }
    if (err != ESP_OK) {
        launcherConsolePrintf("Error finding l_wifi entry, error: %s\n", esp_err_to_name(err));
        nvs_close(rawHandle);
        return false;
    }

    while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        String key = String(info.key);
        if (key.startsWith("s_")) {
            size_t ssid_size = 0;
            err = nvs_get_str(rawHandle, info.key, NULL, &ssid_size);
            if (err != ESP_OK) {
                launcherConsolePrintf("Error %d looking for %s\n", err, info.key);
                break;
            }

            char *ssidBuff = static_cast<char *>(malloc(ssid_size));
            if (!ssidBuff) {
                launcherConsolePrintln("Failed to allocate buffer for SSID");
                break;
            }

            err = nvs_get_str(rawHandle, info.key, ssidBuff, &ssid_size);
            if (err == ESP_OK) {
                String suffix = key.substring(2);
                String pwdKey = "p_" + suffix;
                size_t pwd_size = 0;
                String pwdValue;
                if (nvs_get_str(rawHandle, pwdKey.c_str(), NULL, &pwd_size) == ESP_OK) {
                    char *pwdBuff = static_cast<char *>(malloc(pwd_size));
                    if (pwdBuff) {
                        if (nvs_get_str(rawHandle, pwdKey.c_str(), pwdBuff, &pwd_size) == ESP_OK) {
                            pwdValue = String(pwdBuff);
                        } else {
                            launcherConsolePrintf("Error retrieving pwd for key %s\n", pwdKey.c_str());
                        }
                        free(pwdBuff);
                    } else {
                        launcherConsolePrintln("Failed to allocate buffer for password");
                    }
                } else {
                    launcherConsolePrintf("Password key %s not found\n", pwdKey.c_str());
                }

                String secKey2 = "b_" + suffix;
                uint8_t isSecure = 0;
                nvs_get_u8(rawHandle, secKey2.c_str(), &isSecure);
                if (isSecure) pwdValue = wifiPwdDecrypt(pwdValue);

                setWifiCredential(String(ssidBuff), pwdValue, false);
                launcherConsolePrintf("SSID: %s\n", ssidBuff);
            } else {
                launcherConsolePrintf("Error %d retrieving %s\n", err, info.key);
            }
            free(ssidBuff);
        }

        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    nvs_close(rawHandle);
    return true;
}

/*********************************************************************
**  Function: getConfigs
**  getConfigurations from EEPROM or JSON
**********************************************************************/
void getConfigs() {
    if (!setupSdCard()) {
        getFromNVS();
        getWifiFromNVS();
        return;
    }

    if (!SDM.exists(CONFIG_FILE)) {
        launcherConsolePrintln("getConfigs: config.conf not found, creating with defaults");
        defaultValues();
        saveConfigs();
        return;
    }

    File file = SDM.open(CONFIG_FILE, FILE_READ);
    if (!file) {
        launcherConsolePrintln("getConfigs: failed to open config.conf, resetting to defaults");
        defaultValues();
        saveConfigs();
        return;
    }

    favorite = JsonArray();
    DeserializationError error = deserializeJson(settings, file);
    file.close();

    if (error) {
        launcherConsolePrintf("getConfigs: parse error (%s), resetting to defaults", error.c_str());
        favorite = JsonArray();
        settings.clear();
        defaultValues();
        saveConfigs();
        return;
    }

    log_i("getConfigs: deserialized correctly");
    int count = 0;
    JsonObject setting = settings[0];

    if (setting["onlyBins"].is<bool>()) onlyBins = setting["onlyBins"].as<bool>();
    else {
        count++;
        log_i("getConfigs: missing onlyBins");
    }

    if (setting["bootToApp"].is<bool>()) bootToApp = setting["bootToApp"].as<bool>();
    else {
        count++;
        log_i("getConfigs: missing bootToApp");
    }

    if (setting["noDotFiles"].is<bool>()) noDotFiles = setting["noDotFiles"].as<bool>();
    else {
        count++;
        log_i("getConfigs: missing noDotFiles");
    }

    if (setting["autoBackup"].is<bool>()) autoBackup = setting["autoBackup"].as<bool>();
    else log_i("getConfigs: missing autoBackup");

    if (setting["askSpiffs"].is<bool>()) askSpiffs = setting["askSpiffs"].as<bool>();
    else {
        count++;
        log_i("getConfigs: missing askSpiffs");
    }

    if (setting["bright"].is<int>()) bright = setting["bright"].as<int>();
    else {
        count++;
        log_i("getConfigs: missing bright");
    }

    if (setting["dimmerSet"].is<int>()) dimmerSet = setting["dimmerSet"].as<int>();
    else {
        count++;
        log_i("getConfigs: missing dimmerSet");
    }
    String rot_dev_name = get_efuse_mac_as_string() + "-" + device_name;
    if (setting[get_efuse_mac_as_string()].is<int>()) rotation = setting[get_efuse_mac_as_string()].as<int>();
    else if (setting[rot_dev_name].is<int>()) rotation = setting[rot_dev_name].as<int>();
    else {
        count++;
        log_i("getConfigs: missing rotation");
    }

#ifndef E_PAPER_DISPLAY
    if (setting["FGCOLOR"].is<uint16_t>()) FGCOLOR = setting["FGCOLOR"].as<uint16_t>();
    else {
        count++;
        log_i("getConfigs: missing FGCOLOR");
    }

    if (setting["BGCOLOR"].is<uint16_t>()) BGCOLOR = setting["BGCOLOR"].as<uint16_t>();
    else {
        count++;
        log_i("getConfigs: missing BGCOLOR");
    }

    if (setting["ALCOLOR"].is<uint16_t>()) ALCOLOR = setting["ALCOLOR"].as<uint16_t>();
    else {
        count++;
        log_i("getConfigs: missing ALCOLOR");
    }

    if (setting["odd"].is<uint16_t>()) odd_color = setting["odd"].as<uint16_t>();
    else {
        count++;
        log_i("getConfigs: missing odd");
    }

    if (setting["even"].is<uint16_t>()) even_color = setting["even"].as<uint16_t>();
    else {
        count++;
        log_i("getConfigs: missing even");
    }
#endif

    if (setting["dev"].is<bool>()) dev_mode = setting["dev"].as<bool>();
    else {
        count++;
        log_i("getConfigs: missing dev");
    }

    if (setting["wui_usr"].is<String>()) wui_usr = setting["wui_usr"].as<String>();
    else {
        count++;
        log_i("getConfigs: missing wui_usr");
    }

    if (setting["wui_pwd"].is<String>()) wui_pwd = setting["wui_pwd"].as<String>();
    else {
        count++;
        log_i("getConfigs: missing wui_pwd");
    }

    if (setting["dwn_path"].is<String>()) dwn_path = setting["dwn_path"].as<String>();
    else {
        count++;
        log_i("getConfigs: missing dwn_path");
    }

    if (setting["wifi"].is<JsonArray>()) {
        for (JsonObject wifiEntry : setting["wifi"].as<JsonArray>()) {
            if (wifiEntry["secure"].as<bool>()) {
                wifiEntry["pwd"] = wifiPwdDecrypt(wifiEntry["pwd"].as<String>());
                wifiEntry.remove("secure");
            } else if (!wifiEntry["ssid"].as<String>().isEmpty()) {
                ++count; // plain-text entry — trigger re-save with encryption
            }
        }
    } else {
        ++count;
        log_i("getConfigs: missing wifi");
    }

    if (setting["favorite"].is<JsonArray>()) {
        favorite = setting["favorite"].as<JsonArray>();
    } else {
        ++count;
        log_i("getConfigs: missing favorite");
    }

    if (count > 0) saveConfigs();

    log_i("Brightness: %d", bright);
    setBrightness(bright);
    if (dimmerSet > 120) dimmerSet = 10;
    saveIntoNVS();
    log_i("getConfigs: loaded from config.conf");
}
/*********************************************************************
**  Function: saveConfigs
**  save configs into JSON config.conf file
**********************************************************************/
void saveConfigs() {
    if (!sdcardMounted) {
        saveIntoNVS();
        return;
    }

    // Get (or create) the root settings object — preserves any unknown keys
    JsonObject setting = ensureSettingsRoot();
    if (setting.isNull()) {
        launcherConsolePrintln("saveConfigs: failed to prepare settings root");
        saveIntoNVS();
        return;
    }

    // Ensure favorite array exists
    if (!setting["favorite"].is<JsonArray>()) favorite = setting.createNestedArray("favorite");
    else favorite = setting["favorite"].as<JsonArray>();

    // Ensure wifi array has at least a placeholder entry
    JsonArray wifiList = setting["wifi"].as<JsonArray>();
    if (wifiList.isNull()) wifiList = setting.createNestedArray("wifi");
    if (!wifiList.isNull() && wifiList.size() == 0) {
        JsonObject wifiObj = wifiList.add<JsonObject>();
        if (!wifiObj.isNull()) {
            wifiObj["ssid"] = ssid.length() == 0 ? "myNetSSID" : ssid;
            wifiObj["pwd"] = pwd.length() == 0 ? "myNetPassword" : pwd;
        }
    }

    // Update known settings keys (unknown keys are untouched)
    setting["onlyBins"] = onlyBins;
    setting["bootToApp"] = bootToApp;
    setting["noDotFiles"] = noDotFiles;
    setting["autoBackup"] = autoBackup;
    setting["askSpiffs"] = askSpiffs;
    setting["bright"] = bright;
    setting["dimmerSet"] = dimmerSet;
    String rot_dev_name = get_efuse_mac_as_string() + "-" + device_name;
    setting[rot_dev_name] = rotation;
    setting["FGCOLOR"] = FGCOLOR;
    setting["BGCOLOR"] = BGCOLOR;
    setting["ALCOLOR"] = ALCOLOR;
    setting["odd"] = odd_color;
    setting["even"] = even_color;
    setting["dev"] = dev_mode;
    setting["wui_usr"] = wui_usr;
    setting["wui_pwd"] = wui_pwd;
    setting["dwn_path"] = dwn_path;

    // Encrypt wifi passwords before writing to SD
    {
        JsonArray wl = setting["wifi"].as<JsonArray>();
        if (!wl.isNull()) {
            for (JsonObject e : wl) {
                if (!e["ssid"].as<String>().isEmpty()) {
                    e["pwd"] = wifiPwdEncrypt(e["pwd"].as<String>());
                    e["secure"] = true;
                }
            }
        }
    }

    // Overwrite config.conf directly (FILE_WRITE truncates on open)
    File file = SDM.open(CONFIG_FILE, FILE_WRITE, true);
    size_t written = 0;
    if (file) {
        written = serializeJsonPretty(settings, file);
        file.flush();
        file.close();
    } else {
        launcherConsolePrintln("saveConfigs: failed to open config.conf for writing");
        sdcardMounted = false;
    }

    // Restore plaintext passwords in memory immediately after write
    {
        JsonArray wl = setting["wifi"].as<JsonArray>();
        if (!wl.isNull()) {
            for (JsonObject e : wl) {
                if (e["secure"].as<bool>()) {
                    e["pwd"] = wifiPwdDecrypt(e["pwd"].as<String>());
                    e.remove("secure");
                }
            }
        }
    }

    if (written >= 5) {
        log_i("saveConfigs: config.conf written successfully");
    } else {
        sdcardMounted = false;
        launcherConsolePrintf("saveConfigs: write failed (written=%u)", written);
    }

    saveIntoNVS();
}

#if defined(HAS_RESISTIVE_TOUCH)
#include <CYD28_TouchscreenR.h>

namespace {
constexpr const char *TOUCH_CAL_NAMESPACE = "touch_cal";

bool validTouchCalibration(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1) {
    return x0 > 0 && x1 > 0 && y0 > 0 && y1 > 0 && x0 != x1 && y0 != y1;
}

esp_err_t readTouchCalibrationItems(
    nvs::NVSHandle &handle, uint16_t &x0, uint16_t &x1, uint16_t &y0, uint16_t &y1, uint8_t &rot
) {
    esp_err_t err = handle.get_item("x0", x0);
    err |= handle.get_item("x1", x1);
    err |= handle.get_item("y0", y0);
    err |= handle.get_item("y1", y1);
    err |= handle.get_item("r", rot);
    if (err == ESP_OK) return err;

    err = handle.get_item("x", x0);
    err |= handle.get_item("X", x1);
    err |= handle.get_item("y", y0);
    err |= handle.get_item("Y", y1);
    err |= handle.get_item("r", rot);
    return err;
}
} // namespace

// Reads the raw calibration values from NVS namespace "touch_cal" without
// applying them to the live touch driver. Used by loadTouchCalibration() and by
// the "calibrate show/mirror/swapXY" serial commands, which need to inspect or
// tweak what's persisted without necessarily re-running the wizard.
bool getTouchCalibration(uint16_t &x0, uint16_t &x1, uint16_t &y0, uint16_t &y1, uint8_t &rot) {
    x0 = 0;
    x1 = 0;
    y0 = 0;
    y1 = 0;
    rot = 0;

    esp_err_t err = ESP_OK;
    auto nvsHandle = openNamespace(TOUCH_CAL_NAMESPACE, NVS_READONLY, err);
    if (!nvsHandle) {
        log_i("getTouchCalibration: no %s namespace found", TOUCH_CAL_NAMESPACE);
        return false;
    }

    err = readTouchCalibrationItems(*nvsHandle, x0, x1, y0, y1, rot);
    rot &= 0x07;
    return err == ESP_OK && validTouchCalibration(x0, x1, y0, y1);
}

// Load touch calibration from NVS namespace "touch_cal"
// Returns true if calibration data is found, provides data via parameters
bool loadTouchCalibration() {
    uint16_t x0;
    uint16_t x1;
    uint16_t y0;
    uint16_t y1;
    uint8_t rot;

    if (!getTouchCalibration(x0, x1, y0, y1, rot)) {
        launcherConsolePrintln("loadTouchCalibration: Failed to load valid calibration data");
        return false;
    }

    uint16_t parameters[5] = {x0, x1, y0, y1, rot};
    extern CYD28_TouchR touch;
    touch.setTouch(parameters);
    launcherConsolePrintf(
        "loadTouchCalibration: Loaded calibration - x0:%u x1:%u y0:%u y1:%u rot:%u\n", x0, x1, y0, y1, rot
    );
    return true;
}

// Save touch calibration to NVS namespace "touch_cal"
bool saveTouchCalibration(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint8_t rot) {
    if (!validTouchCalibration(x0, x1, y0, y1)) {
        launcherConsolePrintf(
            "saveTouchCalibration: Invalid calibration - x0:%u x1:%u y0:%u y1:%u rot:%u\n",
            x0,
            x1,
            y0,
            y1,
            rot
        );
        return false;
    }

    esp_err_t err = ESP_OK;
    auto nvsHandle = openNamespace(TOUCH_CAL_NAMESPACE, NVS_READWRITE, err);
    if (!nvsHandle) {
        launcherConsolePrintf("saveTouchCalibration: Failed to open %s namespace\n", TOUCH_CAL_NAMESPACE);
        return false;
    }

    rot &= 0x07;
    err |= nvsHandle->set_item("x0", x0);
    err |= nvsHandle->set_item("x1", x1);
    err |= nvsHandle->set_item("y0", y0);
    err |= nvsHandle->set_item("y1", y1);
    err |= nvsHandle->set_item("r", rot);

    if (err == ESP_OK) { err = nvsHandle->commit(); }

    if (err == ESP_OK) {
        launcherConsolePrintf(
            "saveTouchCalibration: Saved calibration - x0:%u x1:%u y0:%u y1:%u rot:%u\n", x0, x1, y0, y1, rot
        );
        return true;
    }

    launcherConsolePrintf(
        "saveTouchCalibration: Failed to save calibration data: %s\n", esp_err_to_name(err)
    );
    return false;
}
void calibrateTouch() {
    extern CYD28_TouchR touch; // Objeto declarado nos interface.cpp que usam essa biblioteca.
    tft->setRotation(0);
    tft->fillScreen(BGCOLOR);
    wakeUpScreen();
    int saved_dimmerSet = dimmerSet;
    dimmerSet = 0;
    const uint16_t _w = tft->width();
    const uint16_t _h = tft->height();

    struct RawTouchPoint {
        uint16_t x;
        uint16_t y;
    };

    auto drawCenteredLine = [&](const char *text, int16_t y) { tft->drawCentreString(text, _w / 2, y, 1); };

    tft->setTextColor(FGCOLOR, BGCOLOR);
    tft->setTextSize(_fp);
    const int16_t lineHeight = LH;
    int16_t y = (_h - lineHeight * 4) / 2;
    drawCenteredLine("Launcher Touch Calibration", y);
    y += lineHeight;
    drawCenteredLine("---------------------------", y);
    y += lineHeight;
    drawCenteredLine("Touch the screen corners", y);
    y += lineHeight;
    drawCenteredLine("indicated by the arrows", y);
    launcherDelayMs(500);

    auto drawArrow = [&](uint8_t corner) {
        tft->fillRect(0, 0, 30, 30, BGCOLOR);
        tft->fillRect(0, _h - 30, 30, 30, BGCOLOR);
        tft->fillRect(_w - 30, 0, 30, 30, BGCOLOR);
        tft->fillRect(_w - 30, _h - 30, 30, 30, BGCOLOR);
        const int16_t edge = 0;
        const int16_t len = 28;
        const int16_t head = 8;
        const bool right = corner == 1 || corner == 2;
        const bool bottom = corner >= 2;
        const int16_t x0 = right ? _w - edge : edge;
        const int16_t y0 = bottom ? _h - edge : edge;
        const int16_t sx = right ? -1 : 1;
        const int16_t sy = bottom ? -1 : 1;

        tft->drawLine(x0 + sx * len, y0 + sy * len, x0, y0, FGCOLOR);
        tft->drawLine(x0, y0, x0 + sx * head, y0, FGCOLOR);
        tft->drawLine(x0, y0, x0, y0 + sy * head, FGCOLOR);
        tft->drawLine(x0 + 1, y0 + sy, x0 + sx * (head + 1), y0 + sy, FGCOLOR);
        tft->drawLine(x0 + sx, y0 + 1, x0 + sx, y0 + sy * (head + 1), FGCOLOR);
    };

    auto readRawPoint = [&]() {
        while (touch.touched()) { launcherDelayMs(10); }
        while (!touch.touched()) { launcherDelayMs(10); }

        uint32_t sx = 0;
        uint32_t sy = 0;
        const uint8_t samples = 6;
        for (uint8_t i = 0; i < samples; ++i) {
            auto p = touch.getPointRaw();
            sx += p.x;
            sy += p.y;
            launcherDelayMs(18);
        }

        while (touch.touched()) { launcherDelayMs(10); }
        return RawTouchPoint{uint16_t(sx / samples), uint16_t(sy / samples)};
    };

    RawTouchPoint p[4];
    for (uint8_t i = 0; i < 4; ++i) {
        drawArrow(i);
        p[i] = readRawPoint();
    }

    const int32_t leftRawX = (int32_t(p[0].x) + p[3].x) / 2;
    const int32_t rightRawX = (int32_t(p[1].x) + p[2].x) / 2;
    const int32_t topRawX = (int32_t(p[0].x) + p[1].x) / 2;
    const int32_t bottomRawX = (int32_t(p[2].x) + p[3].x) / 2;

    const int32_t leftRawY = (int32_t(p[0].y) + p[3].y) / 2;
    const int32_t rightRawY = (int32_t(p[1].y) + p[2].y) / 2;
    const int32_t topRawY = (int32_t(p[0].y) + p[1].y) / 2;
    const int32_t bottomRawY = (int32_t(p[2].y) + p[3].y) / 2;

    const uint8_t swapXY = abs(rightRawX - leftRawX) < abs(rightRawY - leftRawY);
    const uint8_t invertX = swapXY ? p[0].y > p[1].y : p[0].x > p[1].x;
    const uint8_t invertY = swapXY ? p[0].x > p[3].x : p[0].y > p[3].y;
    // swapXY and invertX are inverted to keep compatibility with current interface.cpp files..
    // need to adjust orientation on the lib to some stardard code and adjust the interface.cpp of the devices
    // that uses it. I´m not doint it now because I dont have all affected devices, jusk have a brief
    // knowladge of what is currently working
    const uint8_t rot_true = swapXY | (invertX << 1) | (invertY << 2); // must use this in the future (or not)
    // remove ! in a near future, and fix the invertedY and X positions
    const uint8_t rot = !swapXY | (invertY << 1) | (!invertX << 2);
    uint16_t xMin = !swapXY ? p[0].y : p[0].x; // remove ! in a near furute
    uint16_t xMax = xMin;
    uint16_t yMin = !swapXY ? p[0].x : p[0].y; // remove ! in a near furute
    uint16_t yMax = yMin;
    for (uint8_t i = 1; i < 4; ++i) {
        const uint16_t rx = !swapXY ? p[i].y : p[i].x; // remove ! in a near furute
        const uint16_t ry = !swapXY ? p[i].x : p[i].y; // remove ! in a near furute
        if (rx < xMin) xMin = rx;
        if (rx > xMax) xMax = rx;
        if (ry < yMin) yMin = ry;
        if (ry > yMax) yMax = ry;
    }

    uint16_t parameters[5] = {xMin, xMax, yMin, yMax, rot};
    touch.setTouch(parameters);
    saveTouchCalibration(xMin, xMax, yMin, yMax, rot);

    launcherConsolePrintf(
        "calibrateTouch: x0:%u x1:%u y0:%u y1:%u rot:%u (rot_true:%u) swap:%u invX:%u invY:%u\n",
        xMin,
        xMax,
        yMin,
        yMax,
        rot,
        rot_true,
        swapXY,
        invertX,
        invertY
    );
    tft->setRotation(rotation);
    tft->fillScreen(BGCOLOR);
    wakeUpScreen();
    dimmerSet = saved_dimmerSet;
}
#endif
