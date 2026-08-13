#ifndef __SETTINGS_H
#define __SETTINGS_H

#include <ArduinoJson.h>

/*
config.conf JSON structure

[
   {
      "rot": 1,
      "onlyBins":1,
      "noDotFiles":1,
      "bright":100,
      "askSpiffs":1,
      "bootToApp":1,
      "wui_usr":"admin",
      "wui_pwd":"launcher",
      "dwn_path": "/downloads/",
      "FGCOLOR":2016,
      "BGCOLOR":0,
      "ALCOLOR":63488,
      "even":13029,
      "odd": 12485,
      "wifi":[
         {
            "ssid":"myNetSSID",
            "pwd":"myNetPassword",
         },
      ]
   }
]

*/
void settings_menu();
void _setBrightness(uint8_t brightval) __attribute__((weak));
void setBrightnessMenu();
void setBrightness(int bright, bool save = true);
void getBrightness();
int gsetRotation(bool set = false);
void getConfigs();
void saveConfigs();
bool saveIntoNVS();
bool saveWifiIntoNVS();
bool getFromNVS();
bool getWifiFromNVS();
bool ensureM5StackUiFlowNVSDefaults();
bool getWifiCredential(const String &ssid, String &password);
bool setWifiCredential(const String &ssid, const String &password, bool persist = false);
bool removeWifiCredential(const String &ssid);
bool clearWifiCredentials();
void setdimmerSet();
void setUiColor();
void chargeMode();
JsonObject ensureSettingsRoot();
bool saveSessionToken(const String &token);
String loadSessionToken();

bool getKeyBinding(const String &key, String &outPath);
bool setKeyBinding(const String &key, const String &path, bool persist = true);
bool removeKeyBinding(const String &key, bool persist = true);
bool clearKeyBindings();

#if defined(HAS_RESISTIVE_TOUCH)
bool loadTouchCalibration();
bool saveTouchCalibration(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint8_t rot);
bool getTouchCalibration(uint16_t &x0, uint16_t &x1, uint16_t &y0, uint16_t &y1, uint8_t &rot);
void calibrateTouch();
#endif

#endif
