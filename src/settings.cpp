
#include "settings.h"
#include "display.h"
#include "esp_mac.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "partitioner.h"
#include "sd_functions.h"
#include <globals.h>
/**************************************************************************************
EEPROM ADDRESSES MAP


0	N Rot 	    16		    32	Pass	48	Pass	64	Pass	80	Pass	96		112
1	N Dim	    17		    33	Pass	49	Pass	65	Pass	81	Pass	97		113
2	N Bri       18		    34	Pass  	50	Pass	66	Pass	82	Pass	98		114
3	N	        19		    35	Pass	51	Pass	67	Pass	83	Pass	99		115	(L- Brigh)
4	N	        20	Pass  	36	Pass	52	Pass	68	Pass	84	Pass    100		116	(L- Dim)
5	N	        21	Pass  	37	Pass  	53	Pass	69	Pass	85		    101		117	(L- Rotation)
6	 	        22	Pass  	38	Pass  	54	Pass	70	Pass	86		    102		118	(L-odd)
7	 	        23	Pass  	39	Pass  	55	Pass	71	Pass	87		    103		119	(L-odd)
8	 	        24	Pass  	40	Pass  	56	Pass	72	Pass	88		    104		120	(L-even)
9	 	        25	Pass  	41	Pass  	57	Pass	73	Pass	89		    105		121	(L-even)
10  	        26	Pass  	42	Pass  	58	Pass	74	Pass	90 miso     106		122	(L-BGCOLOR)
11  	        27	Pass  	43	Pass  	59	Pass	75	Pass	91 mosi     107		123	(L-BGCOLOR)
12  	        28	Pass  	44	Pass  	60	Pass	76	Pass	92 sck      108		124	(L-FGCOLOR)
13		        29	Pass  	45	Pass  	61	Pass	77	Pass	93 cs	    109		125	(L-FGCOLOR)
14		        30	Pass	46	Pass  	62	Pass	78	Pass	94		    110		126	(L-AskSpiffs)
15		        31	Pass  	47	Pass  	63	Pass	79	Pass	95		    111		127	(L-OnlyBins)

From 1 to 5: Nemo shared addresses
(L -*) stands for Launcher addresses

***************************************************************************************/

void settings_menu() {
    options = {
#ifndef E_PAPER_DISPLAY
        {"Charge Mode", [=]() { chargeMode(); }},
#endif
        {"Brightness",
                               [=]() {
             setBrightnessMenu();
             saveConfigs();
         }                                                           },
        {"Dim time",
                               [=]() {
             setdimmerSet();
             saveConfigs();
         }                                                           },
#ifndef E_PAPER_DISPLAY
        {"UI Color",    [=]() {
             setUiColor();
             saveConfigs();
         }                 },
#endif
    };
    if (sdcardMounted) {
        if (onlyBins)
            options.push_back({"All Files", [=]() {
                                   gsetOnlyBins(true, false);
                                   saveConfigs();
                               }});
        else
            options.push_back({"Only Bins", [=]() {
                                   gsetOnlyBins(true, true);
                                   saveConfigs();
                               }});
    }

    if (askSpiffs)
        options.push_back({"Avoid Spiffs", [=]() {
                               gsetAskSpiffs(true, false);
                               saveConfigs();
                           }});
    else
        options.push_back({"Ask Spiffs", [=]() {
                               gsetAskSpiffs(true, true);
                               saveConfigs();
                           }});
#ifndef E_PAPER_DISPLAY
    options.push_back({"Orientation", [=]() {
                           gsetRotation(true);
                           saveConfigs();
                       }});
#endif
#if !defined(CORE_4MB) && defined(M5STACK)
    options.push_back({"Partition Change", [=]() { partitioner(); }});
    options.push_back({"List of Partitions", [=]() { partList(); }});
#endif

#ifndef PART_04MB
    options.push_back({"Clear FAT", [=]() { eraseFAT(); }});
#endif

    if (MAX_SPIFFS > 0)
        options.push_back({"Backup SPIFFS", [=]() { dumpPartition("spiffs", "/bkp/spiffs"); }});
    if (MAX_FAT_sys > 0 && dev_mode)
        options.push_back({"Backup FAT sys", [=]() { dumpPartition("sys", "/bkp/FAT_sys"); }}); // Test only
    if (MAX_FAT_vfs > 0)
        options.push_back({"Backup FAT vfs", [=]() { dumpPartition("vfs", "/bkp/FAT_vfs"); }});
    if (MAX_SPIFFS > 0) options.push_back({"Restore SPIFFS", [=]() { restorePartition("spiffs"); }});
    if (MAX_FAT_sys > 0 && dev_mode)
        options.push_back({"Restore FAT Sys", [=]() { restorePartition("sys"); }}); // Test only
    if (MAX_FAT_vfs > 0) options.push_back({"Restore FAT Vfs", [=]() { restorePartition("vfs"); }});
    if (dev_mode) options.push_back({"Boot Animation", [=]() { initDisplayLoop(); }});
    if (dev_mode) options.push_back({"Deactivate Dev", [=]() { dev_mode = false; }});
    options.push_back({"Restart", [=]() { FREE_TFT ESP.restart(); }});
#if defined(STICK_C_PLUS2) || defined(T_EMBED) || defined(STICK_C_PLUS) || defined(T_LORA_PAGER)
    options.push_back({"Turn-off", [=]() { powerOff(); }});
#endif

    options.push_back({"Main Menu", [=]() { returnToMenu = true; }});
    loopOptions(options);
    tft->drawPixel(0, 0, 0);
    tft->fillScreen(BGCOLOR);
}

// This function comes from interface.h
void _setBrightness(uint8_t brightval) {}

/*********************************************************************
**  Function: setBrightness
**  save brightness value into EEPROM
**********************************************************************/
void setBrightness(int brightval, bool save) {
    if (brightval > 100) brightval = 100;

#if defined(HEADLESS)
// do Nothing
#else
    _setBrightness(brightval);
#endif

    if (save) {
        EEPROM.begin(EEPROMSIZE); // open eeprom
        bright = brightval;
        EEPROM.write(EEPROMSIZE - 15, brightval); // set the byte
        EEPROM.commit();                          // Store data to EEPROM
        EEPROM.end();                             // Free EEPROM memory
    }
}

/*********************************************************************
**  Function: getBrightness
**  save brightness value into EEPROM
**********************************************************************/
void getBrightness() {
    EEPROM.begin(EEPROMSIZE);
    bright = EEPROM.read(EEPROMSIZE - 15);
    EEPROM.end(); // Free EEPROM memory
    if (bright > 100) {
        bright = 100;

#if defined(HEADLESS)
// do Nothing
#else
        _setBrightness(bright);
#endif
        setBrightness(100);
    }

#if defined(HEADLESS)
// do Nothing
#else
    _setBrightness(bright);
#endif
}

/*********************************************************************
**  Function: gsetOnlyBins
**  get onlyBins from EEPROM
**********************************************************************/
bool gsetOnlyBins(bool set, bool value) {
    EEPROM.begin(EEPROMSIZE);
    int onlyBin = EEPROM.read(EEPROMSIZE - 1);
    bool result = false;

    if (onlyBin > 1) { set = true; }

    if (onlyBin == 0) result = false;
    else result = true;

    if (set) {
        result = value;
        onlyBins = value; // update the global variable
        EEPROM.write(EEPROMSIZE - 1, result);
        EEPROM.commit();
    }
    EEPROM.end(); // Free EEPROM memory
    return result;
}

/*********************************************************************
**  Function: gsetAskSpiffs
**  get onlyBins from EEPROM
**********************************************************************/
bool gsetAskSpiffs(bool set, bool value) {
    EEPROM.begin(EEPROMSIZE);
    int spiffs = EEPROM.read(EEPROMSIZE - 2);
    bool result = false;

    if (spiffs > 1) { set = true; }

    if (spiffs == 0) result = false;
    else result = true;

    if (set) {
        result = value;
        askSpiffs = value; // update the global variable
        EEPROM.write(EEPROMSIZE - 2, result);
        EEPROM.commit();
    }
    EEPROM.end(); // Free EEPROM memory
    return result;
}

/*********************************************************************
**  Function: gsetRotation
**  get onlyBins from EEPROM
**********************************************************************/
#if ROTATION == 0
#define DRV 0
#else
#define DRV 1
#endif
int gsetRotation(bool set) {
    EEPROM.begin(EEPROMSIZE);
    int getRot = EEPROM.read(EEPROMSIZE - 13);
    int result = ROTATION;

    if (getRot > 3) {
        set = true;
        result = ROTATION;
    } else result = getRot;

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
            tftHeight = TFT_WIDTH - (FM * LH + 4);
            ;
#else
            tftHeight = TFT_WIDTH;
#endif
            tftWidth = TFT_HEIGHT;
        } else {
#if defined(HAS_TOUCH)
            tftHeight = TFT_HEIGHT - (FM * LH + 4);
#else
            tftHeight = TFT_HEIGHT;
#endif
            tftWidth = TFT_WIDTH;
        }

        tft->setRotation(result);
        EEPROM.write(EEPROMSIZE - 13, result);
        EEPROM.commit();
        tft->fillScreen(BGCOLOR);
    }
    EEPROM.end(); // Free EEPROM memory
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
        {" 0 %", [=]() { setBrightness(1); }  },
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
    EEPROM.begin(EEPROMSIZE);

    EEPROM.write(EEPROMSIZE - 3, int((FGCOLOR >> 8) & 0x00FF));
    EEPROM.write(EEPROMSIZE - 4, int(FGCOLOR & 0x00FF));

    EEPROM.write(EEPROMSIZE - 5, int((BGCOLOR >> 8) & 0x00FF));
    EEPROM.write(EEPROMSIZE - 6, int(BGCOLOR & 0x00FF));

    EEPROM.write(EEPROMSIZE - 7, int((ALCOLOR >> 8) & 0x00FF));
    EEPROM.write(EEPROMSIZE - 8, int(ALCOLOR & 0x00FF));

    EEPROM.write(EEPROMSIZE - 9, int((odd_color >> 8) & 0x00FF));
    EEPROM.write(EEPROMSIZE - 10, int(odd_color & 0x00FF));

    EEPROM.write(EEPROMSIZE - 11, int((even_color >> 8) & 0x00FF));
    EEPROM.write(EEPROMSIZE - 12, int(even_color & 0x00FF));

    EEPROM.commit(); // Store data to EEPROM
    EEPROM.end();
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
    EEPROM.begin(EEPROMSIZE);
    EEPROM.write(EEPROMSIZE - 14, dimmerSet); // 20s Dimm time
    EEPROM.commit();
    EEPROM.end();
}

/*********************************************************************
**  Function: chargeMode
**  Enter in Charging mode
**********************************************************************/
void chargeMode() {
#ifndef CONFIG_IDF_TARGET_ESP32P4
    setCpuFrequencyMhz(80);
#endif
    setBrightness(5, false);
    vTaskDelay(pdTICKS_TO_MS(500));
    tft->fillScreen(BGCOLOR);
    unsigned long tmp = 0;
    while (!check(SelPress)) {
        if (millis() - tmp > 5000) {
            displayRedStripe(String(getBattery()) + " %");
            tmp = millis();
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
bool config_exists() {
    if (!SDM.exists(CONFIG_FILE)) {
        File conf = SDM.open(CONFIG_FILE, FILE_WRITE, true);
        ;
        if (conf) {
            conf.printf(
                "[{\"%s\":%d,\"dimmerSet\":10,\"onlyBins\":1,\"bright\":100,\"askSpiffs\":1,\"wui_usr\":"
                "\"admin\",\"wui_pwd\":\"launcher\",\"dwn_path\":\"/downloads/"
                "\",\"FGCOLOR\":2016,\"BGCOLOR\":0,\"ALCOLOR\":63488,\"even\":13029,\"odd\":12485,\",\"dev\":"
                "0,\"wifi\":[{\"ssid\":\"myNetSSID\",\"pwd\":\"myNetPassword\"}], \"favorite\":[]}]",
                get_efuse_mac_as_string().c_str(),
                ROTATION
            );
        }
        conf.close();
        vTaskDelay(pdTICKS_TO_MS(50));
        log_i("config_exists: config.conf created with default");
        return false;
    } else {
        log_i("config_exists: config.conf exists");
        return true;
    }
}

/*********************************************************************
**  Function: getConfigs
**  getConfigurations from EEPROM or JSON
**********************************************************************/
void getConfigs() {
    if (setupSdCard()) {
        // check if config file exists, otherwise create it with default values
        config_exists();
        File file = SDM.open(CONFIG_FILE, FILE_READ);
        if (file) {
            DeserializationError error = deserializeJson(settings, file);
            if (error) {
                log_i("Failed to read file, using default configuration");
                goto Default;
            } else {
                log_i("getConfigs: deserialized correctly");
            }

            int count = 0;
            JsonObject setting = settings[0];
            if (setting["onlyBins"].is<bool>()) {
                onlyBins = gsetOnlyBins(setting["onlyBins"].as<bool>());
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["askSpiffs"].is<bool>()) {
                askSpiffs = gsetAskSpiffs(setting["askSpiffs"].as<bool>());
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["bright"].is<int>()) {
                bright = setting["bright"].as<int>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["dimmerSet"].is<int>()) {
                dimmerSet = setting["dimmerSet"].as<int>();
            } else {
                count++;
                log_i("Fail");
            }
            char *mac;
            if (setting[get_efuse_mac_as_string()].is<int>()) {
                rotation = setting[get_efuse_mac_as_string()].as<int>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["FGCOLOR"].is<uint16_t>()) {
                FGCOLOR = setting["FGCOLOR"].as<uint16_t>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["BGCOLOR"].is<uint16_t>()) {
                BGCOLOR = setting["BGCOLOR"].as<uint16_t>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["ALCOLOR"].is<uint16_t>()) {
                ALCOLOR = setting["ALCOLOR"].as<uint16_t>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["odd"].is<uint16_t>()) {
                odd_color = setting["odd"].as<uint16_t>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["even"].is<uint16_t>()) {
                even_color = setting["even"].as<uint16_t>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["dev"].is<bool>()) {
                dev_mode = setting["dev"].as<bool>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["wui_usr"].is<String>()) {
                wui_usr = setting["wui_usr"].as<String>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["wui_pwd"].is<String>()) {
                wui_pwd = setting["wui_pwd"].as<String>();
            } else {
                count++;
                log_i("Fail");
            }
            if (setting["dwn_path"].is<String>()) {
                dwn_path = setting["dwn_path"].as<String>();
            } else {
                count++;
                log_i("Fail");
            }
            if (!setting["wifi"].is<JsonArray>()) {
                ++count;
                log_i("Fail");
            }
            if (setting["favorite"].is<JsonArray>()) {
                favorite = setting["favorite"].as<JsonArray>();
            } else {
                ++count;
                log_i("Fail");
            }
            if (count > 0) saveConfigs();

            log_i("Brightness: %d", bright);
            setBrightness(bright);
            if (dimmerSet > 120) dimmerSet = 10;

            file.close();

            EEPROM.begin(EEPROMSIZE);
            EEPROM.write(EEPROMSIZE - 13, rotation);
            EEPROM.write(EEPROMSIZE - 14, dimmerSet);
            EEPROM.write(EEPROMSIZE - 15, bright);
            EEPROM.write(EEPROMSIZE - 1, int(onlyBins));
            EEPROM.write(EEPROMSIZE - 2, int(askSpiffs));

            EEPROM.write(EEPROMSIZE - 3, int((FGCOLOR >> 8) & 0x00FF));
            EEPROM.write(EEPROMSIZE - 4, int(FGCOLOR & 0x00FF));

            EEPROM.write(EEPROMSIZE - 5, int((BGCOLOR >> 8) & 0x00FF));
            EEPROM.write(EEPROMSIZE - 6, int(BGCOLOR & 0x00FF));

            EEPROM.write(EEPROMSIZE - 7, int((ALCOLOR >> 8) & 0x00FF));
            EEPROM.write(EEPROMSIZE - 8, int(ALCOLOR & 0x00FF));

            EEPROM.write(EEPROMSIZE - 9, int((odd_color >> 8) & 0x00FF));
            EEPROM.write(EEPROMSIZE - 10, int(odd_color & 0x00FF));

            EEPROM.write(EEPROMSIZE - 11, int((even_color >> 8) & 0x00FF));
            EEPROM.write(EEPROMSIZE - 12, int(even_color & 0x00FF));

            if (!EEPROM.commit()) log_i("fail to write EEPROM");
            EEPROM.end();
            log_i("Using config.conf setup file");
        } else {
        Default:
            file.close();
            saveConfigs();
            log_i("Using settings stored on EEPROM");
        }
    }
}
/*********************************************************************
**  Function: saveConfigs
**  save configs into JSON config.conf file
**********************************************************************/
void saveConfigs() {
    bool retry = true;

    while (true) {
        if (!setupSdCard()) break;

        if (SDM.remove(CONFIG_FILE)) log_i("config.conf deleted");
        else log_i("fail deleting config.conf");

        JsonArray settingsArray = settings.as<JsonArray>();
        if (settingsArray.isNull()) {
            settings.clear();
            settingsArray = settings.to<JsonArray>();
        }
        if (settingsArray.isNull()) {
            log_e("saveConfigs: failed to prepare settings array");
            break;
        }

        JsonObject setting;
        if (settingsArray.size() > 0 && settingsArray[0].is<JsonObject>()) {
            setting = settingsArray[0];
        } else {
            settingsArray.clear();
            setting = settingsArray.add<JsonObject>();
        }
        if (setting.isNull()) { setting = settingsArray.add<JsonObject>(); }
        if (setting.isNull()) {
            log_e("saveConfigs: failed to create root object");
            break;
        }
        favorite = setting["favorite"].as<JsonArray>();
        if (favorite.isNull()) favorite = setting.createNestedArray("favorite");

        JsonArray wifiList = setting["wifi"].as<JsonArray>();
        if (wifiList.isNull()) { wifiList = setting.createNestedArray("wifi"); }
        if (wifiList.isNull()) {
            log_e("saveConfigs: failed to create wifi array");
            break;
        }
        if (wifiList.size() == 0) {
            JsonObject wifiObj = wifiList.add<JsonObject>();
            if (wifiObj.isNull()) { wifiObj = wifiList.add<JsonObject>(); }
            if (!wifiObj.isNull()) {
                wifiObj["ssid"] = ssid.length() == 0 ? "myNetSSID" : ssid;
                wifiObj["pwd"] = pwd.length() == 0 ? "myNetPassword" : pwd;
            } else {
                log_e("saveConfigs: failed to allocate default wifi entry");
            }
        }
        // Update JSON document with current configuration
        setting["onlyBins"] = onlyBins;
        setting["askSpiffs"] = askSpiffs;
        setting["bright"] = bright;
        setting["dimmerSet"] = dimmerSet;
        setting[get_efuse_mac_as_string()] = rotation;
        setting["FGCOLOR"] = FGCOLOR;
        setting["BGCOLOR"] = BGCOLOR;
        setting["ALCOLOR"] = ALCOLOR;
        setting["odd"] = odd_color;
        setting["even"] = even_color;
        setting["dev"] = dev_mode;
        setting["wui_usr"] = wui_usr;
        setting["wui_pwd"] = wui_pwd;
        setting["dwn_path"] = dwn_path;

        File file = SDM.open(CONFIG_FILE, FILE_WRITE, true);
        if (!file) {
            log_i("Failed to create file");
            break;
        }
        log_i("config.conf created");

        size_t written = serializeJsonPretty(settings, file);
        file.flush();
        file.close();

        if (written < 5) {
            if (retry) {
                log_i("Failed to write to file");
                SDM.remove(CONFIG_FILE);
                log_i("Creating default file");
                config_exists();
                File defaultFile = SDM.open(CONFIG_FILE, FILE_READ);
                if (defaultFile) {
                    DeserializationError err = deserializeJson(settings, defaultFile);
                    if (err) {
                        log_i("Failed to deserialize default config: %s", err.c_str());
                        settings.clear();
                    }
                    defaultFile.close();
                } else {
                    log_i("Failed to reopen config.conf for recovery");
                }
                retry = false;
                continue;
            }
            log_i("Create new file and Rewriting didn't work");
        } else {
            log_i("config.conf written successfully");
        }

        break;
    }

    EEPROM.begin(EEPROMSIZE + 32);
    EEPROM.writeString(20, pwd);
    EEPROM.writeString(EEPROMSIZE, ssid);
    EEPROM.commit();
    EEPROM.end();
}
