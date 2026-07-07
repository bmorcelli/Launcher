#include <globals.h>

#if defined(HEADLESS)
#include <VectorDisplay.h>
#else
#include <tft.h>
#endif
#include "esp_ota_ops.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "nvs_flash.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "nvs.h"
#include "nvs_handle.hpp"
#endif
#include <SD.h>
#include <SPIFFS.h>

#include "utils.h"
#include "powerSave.h"
#include "ram_profile.h"
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#ifdef USE_CARDKB2
#include <cardkb2.h>
#endif

#if defined(SET_LOOP_TASK_STACK_SIZE)
SET_LOOP_TASK_STACK_SIZE(16384)
#endif

// Public Globals
#ifdef USE_M5GFX
uint16_t FGCOLOR = BLACK;
uint16_t ALCOLOR = BLACK;
uint16_t BGCOLOR = WHITE;
#elif E_PAPER_DISPLAY
uint16_t FGCOLOR = BLACK;
uint16_t ALCOLOR = 0x8888;
uint16_t BGCOLOR = WHITE;
#else
uint16_t FGCOLOR = GREEN;
uint16_t ALCOLOR = RED;
uint16_t BGCOLOR = BLACK;
#endif
uint16_t odd_color = 0x30c5;
uint16_t even_color = 0x32e5;

int8_t _miso = SDCARD_MISO;
int8_t _mosi = SDCARD_MOSI;
int8_t _sck = SDCARD_SCK;
int8_t _cs = SDCARD_CS;

// Navigation Variables
long LongPressTmp = 0;
volatile bool LongPress = false;
volatile bool NextPress = false;
volatile bool PrevPress = false;
volatile bool UpPress = false;
volatile bool DownPress = false;
volatile bool SelPress = false;
volatile bool EscPress = false;
volatile bool AnyKeyPress = false;
LTouchPoint touchPoint;
keyStroke KeyStroke;

#if defined(HAS_TOUCH)
volatile uint16_t tftHeight = TFT_WIDTH - (FM * LH + 4);
#else
volatile uint16_t tftHeight = TFT_WIDTH;
#endif
volatile uint16_t tftWidth = TFT_HEIGHT;
TaskHandle_t xHandle;
void __attribute__((weak)) taskInputHandler(void *parameter) {
    auto timer = launcherMillis();
    while (true) {
        checkPowerSaveTime();
        if (!AnyKeyPress || launcherMillis() - timer > 75) {
            resetGlobals();
#ifndef DONT_USE_INPUT_TASK
            InputHandler();
#ifdef USE_CARDKB2
            cardkb2_poll();
#endif
#endif
            timer = launcherMillis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// More 2nd grade global Variables
int dimmerSet = 20;
unsigned long previousMillis;
bool isSleeping;
bool isScreenOff;
bool dev_mode = false;
int bright = 100;
bool dimmer = false;
int prog_handler; // 0 - Flash, 1 - SPIFFS
int currentIndex;
int rotation = ROTATION;
bool sdcardMounted;
bool onlyBins;
bool bootToApp = true;
bool noDotFiles;
bool autoBackup = true;
bool returnToMenu;
bool update;
bool askSpiffs;

// bool command;
size_t file_size;
String ssid;
String pwd;
String wui_usr = "admin";
String wui_pwd = "launcher";
String dwn_path = "/downloads/";
String lastInstalledApp = "";
uint16_t total_firmware = 0;
uint8_t current_page = 1;
uint8_t num_pages = 0;
JsonDocument doc(launcherJsonAllocator());
JsonArray favorite;
JsonDocument settings;
std::vector<Option> options;

#include "app_registry.h"
#include "display.h"
#include "massStorage.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "partitioner.h"
#include "sd_functions.h"
#include "settings.h"
#include "webInterface.h"

/*********************************************************************
**  Function: _setup_gpio()
**  Sets up a weak (empty) function to be replaced by /ports/* /interface.h
*********************************************************************/
void _setup_gpio() __attribute__((weak));
void _setup_gpio() {}

/*********************************************************************
**  Function: _post_setup_gpio()
**  Sets up a weak (empty) function to be replaced by /ports/* /interface.h
*********************************************************************/
void _post_setup_gpio() __attribute__((weak));
void _post_setup_gpio() {}

/*********************************************************************
**  Function: setup
**  Where the devices are started and variables set
*********************************************************************/
void setup() {
    Serial.begin(115200);
    RAM_LOG("setup-start");
    nvs_flash_init();
    launcherPartitionInitDefaultSizes();
    ensureM5StackUiFlowNVSDefaults();
    RAM_LOG("after-nvs-partition-defaults");

#if CONFIG_IDF_TARGET_ESP32P4
    esp_err_t nve;
    std::unique_ptr<nvs::NVSHandle> nvsHandle = nvs::open_nvs_handle("launcher", NVS_READWRITE, &nve);
    bool init = false;
    nve = nvsHandle->get_item("init", init);
    if (nve != ESP_OK) {
        nvsHandle->set_item("init", false);
        nvsHandle->commit();
        init = false;
    }
    if (init >= 1) { // restart com eeprom em 1
        nvsHandle->set_item("init", false);
        nvsHandle->commit();
        ESP.restart();
    } else {
        nvsHandle->set_item("init", true);
        nvsHandle->commit();
    }
#endif

// Setup GPIOs and stuff
#if defined(HEADLESS)
#if LED > 0
    launcherGpioOutput(LED);        // Set pin to recognize if launcher is starting or connecting
    launcherGpioWrite(LED, LED_ON); // keeps on until exit
#endif
#endif

    _setup_gpio();

    // Get Configuration from NVS partition
    getFromNVS();
    RAM_LOG("after-getFromNVS");

    // declare variables
    size_t currentIndex = 0;
    prog_handler = 0;
    sdcardMounted = false;
    String fileToCopy;

// Init Display
#if !defined(HEADLESS)
    // tft->setAttribute(PSRAM_ENABLE,true);
    tft->begin();
#ifdef TFT_INVERSION_ON
    tft->invertDisplay(true);
#endif

#endif
    tft->setRotation(rotation);
    tft->setTextColor(FGCOLOR, BGCOLOR);
    if (rotation & 0b1) {
#if defined(HAS_TOUCH)
        tftHeight = TFT_WIDTH - (FM * LH + 4);
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
    tft->fillScreen(BGCOLOR);
    setBrightness(bright, false);
    initDisplay(true);
    RAM_LOG("after-first-display");

    // Performs the verification when Launcher is installed through OTA
    partitionCrawler();
    RAM_LOG("after-partitionCrawler");

#if defined(USE_CARDKB2) && defined(CARDKB2_SDA) && defined(CARDKB2_SCL)
    cardkb2_setup(CARDKB2_SDA, CARDKB2_SCL);
#endif

    // Init post setup GPIO before SD Card initializes
    _post_setup_gpio();

#if defined(HAS_RESISTIVE_TOUCH)
    if (!loadTouchCalibration()) calibrateTouch();
#endif
    // Gets the config.conf from SD Card and fill out the settings JSON
    getConfigs();
    RAM_LOG("after-getConfigs");
#if defined(HAS_TOUCH)
    TouchFooter2();
#endif

    // Some boards need input polling to stay on the main loop thread because
    // display/touch drivers are not safe to service from a helper task.
#ifndef DONT_USE_INPUT_TASK
    xTaskCreate(
        taskInputHandler, // Task function
        "InputHandler",   // Task Name
        3500,             // Stack size
        NULL,             // Task parameters
        2,                // Task priority (0 to 3), loopTask has priority 2.
        &xHandle          // Task handle (not used)
    );
#else
    xHandle = nullptr;
#endif

    // Start Bootscreen timer
    int i = launcherMillis();
    int j = 0;
    LongPress = true;
    RAM_LOG("before-bootscreen");
    while (launcherMillis() < i + (2000 + bootToApp * 3000)) { // increased from 2500 to 5000
        initDisplay();                                         // Inicia o display

        if (launcherMillis() > (i + j * 500)) { // Serial message each ~500ms
            launcherConsolePrintln("Press the button to enter the Launcher!");
            j++;
        }
#if defined(HAS_TOUCH)
        // Enable touch the center of the screen to get into Launcher
        if (touchPoint.pressed) {
            LTouchPoint *t = &touchPoint;
            int third_x = tftWidth / 3;
            int third_y = tftHeight / 3;
            if (t->x > third_x * 1 && t->x < third_x * 2 && ((t->y > third_y && t->y < third_y * 2))) {
                tft->fillScreen(BGCOLOR);
                touchPoint.pressed = false;
                goto Launcher;
            }
        }
#endif
        // Direct input check for startup - bypass check() function to avoid task suspension
#if defined(HAS_1_BUTTON)
        if (check(SelPress) || check(NextPress))
#else
        if (check(SelPress))
#endif
        {
            tft->fillScreen(BGCOLOR);
            goto Launcher;
        }

#if defined(HAS_KEYBOARD)
        keyStroke key = _getKeyPress();
        if (key.pressed && !key.enter)
#elif defined(HAS_1_BUTTON)
        if (check(EscPress))
#elif defined(STICK_C_PLUS2) || defined(STICK_C_PLUS)
        if (check(NextPress))
#else
        if (check(AnyKeyPress))
#endif
        {
            launcherBootInstalledAppOrShowMenu();
            goto Launcher;
        }
    }
    // If nothing is done and there's something installed, launch it
    if (launcherBootCurrentApp()) {
        tft->fillScreen(BGCOLOR);
        _setBrightness(0);
        reboot();
    }

// If M5 or Enter button is pressed, continue from here
Launcher:
    RAM_LOG("launcher-label");
    LongPress = false;
    tft->fillScreen(BGCOLOR);
#if LED > 0 && defined(HEADLESS)
    launcherGpioWrite(LED, LED_ON ? LOW : HIGH); // turn off the LED
#endif
}

/**********************************************************************
**  Function: loop
**  Main loop
**********************************************************************/
#ifndef HEADLESS
void loop() {
    static bool loggedFirstLoop = false;
    if (!loggedFirstLoop) {
        RAM_LOG("first-loop-start");
        loggedFirstLoop = true;
    }
    bool redraw = true;
    bool update_sd;
    int index = 0;
    int opt = 5; // there are 3 options> 1 list SD files, 2 OTA, 3 USB and 4 Config
    int pass_by = 0;
    bool first_loop = true;
    getBrightness();
    if (!sdcardMounted) index = 1; // if SD card is not present, paint SD square grey and auto select OTA
    std::vector<MenuOptions> menuItems = {
        {
#if (TFT_HEIGHT < 135) || (TFT_WIDTH < 135)
         "SD", "Launch from SDCard",
#else
            "SD",
            "Launch from or mng SDCard",
#endif
         [=]() { loopSD(false); },
         sdcardMounted
        },
#ifndef DISABLE_OTA
        {"OTA", "Online Installer", [=]() { ota_function(); }},
#endif
        {
#if (TFT_HEIGHT < 135) || (TFT_WIDTH < 135)
         "WUI", "Start WebUI",
#else
            "WUI",
            "Start Web User Interface",
#endif
         [=]() { loopOptionsWebUi(); }
        },
#if defined(SOC_USB_OTG_SUPPORTED)
        {
#if (TFT_HEIGHT < 135) || (TFT_WIDTH < 135)
         "USB", "SD->USB",
#else
            "USB",
            "SD->USB Interface",
#endif
         [=]() {
                if (setupSdCard()) {
                    MassStorage();
                    tft->drawPixel(0, 0, 0);
                    tft->fillScreen(BGCOLOR);
                } else {
                    displayRedStripe("Insert SD Card");
                    launcherDelayMs(2000);
                }
            }, sdcardMounted
        },
#endif
        {
#if (TFT_HEIGHT < 135) || (TFT_WIDTH < 135)
         "PM"
#else
            "PMan"
#endif
            ,
         "Partition Manager.", [=]() { partList(); }
        },
        {
#if (TFT_HEIGHT < 135) || (TFT_WIDTH < 135)
         "CFG", "Change Settings.",
#else
            "CFG",
            "Change Launcher Settings.",
#endif
         [=]() { settings_menu(); }
        }
    };
    if (first_loop) RAM_LOG("first-mainMenu-built");

    for (const LauncherAppMetadata &app : launcherListInstalledApps()) {
        String appLabel = app.label;
        String appName = app.name.isEmpty() ? app.label : app.name;
        String appIcon = app.name.substring(0, 5);
        appIcon.toUpperCase();
        menuItems.push_back(
            {appIcon,
             appName,
             [appLabel]() { launcherShowAppActions(appLabel.c_str()); },
             true,
             false,
             0,
             0,
             0,
             0,
             ALCOLOR}
        );
    }

#if !defined(CARDPUTER)
    menuItems.push_back(
        // Add power off option for devices that are not easy to turn off
        // on e-paper, it keeps the Launcher bootscreen printed
        {"OFF", "Turn off Device", [=]() { powerOff(); }}
    );
#endif
    opt = menuItems.size(); // number of options in the menu
    update_sd = sdcardMounted;
    while (1) {
        if (redraw) {
            if (update_sd != sdcardMounted) {
                for (auto o : menuItems) {
                    if (o.name == "SD") o.active = sdcardMounted;
                    if (o.name == "USB") o.active = sdcardMounted;
                }
                update_sd = sdcardMounted;
            }
            if (!dev_mode && pass_by == 5) {
                displayRedStripe("Dev mode Activated");
                vTaskDelay(2000 / portTICK_PERIOD_MS);
                dev_mode = true;
            }
            drawMainMenu(menuItems, index);
#if defined(HAS_TOUCH)
            TouchFooter();
#endif
            redraw = false;
            LongPress = false;
            returnToMenu = false;
            tft->display(false);
            if (first_loop) {
                first_loop = false;
                launcherDelayMs(350);
                resetGlobals(); // avoid leaking command after menu is shown
            }
        }
        if (touchPoint.pressed) {
            int i = 0;
            for (auto item : menuItems) {
                if (item.contain(touchPoint.x, touchPoint.y)) {
                    resetGlobals();
#ifndef E_PAPER_DISPLAY
                    if (i == index) {
                        item.action();
                        tft->drawPixel(0, 0, 0);
                        tft->fillScreen(BGCOLOR);
                    } else {
                        index = i;
                        drawMainMenu(menuItems, index); // Redraw the menu to show the selected item
                        break;
                    }
#else
                    item.action(); // Call the action associated with the selected menu item
#endif
                    returnToMenu = false;
                    redraw = true;
                    goto END;
                }
                i++;
            }
            touchPoint.Clear();
        }
        if (check(PrevPress)) {
            if (index == 0) index = opt - 1;
            else if (index > 0) index--;
            pass_by = 0;
            redraw = true;
        }
        // DW Btn to next item
        if (check(NextPress)) {
            index++;
            if ((index + 1) > opt) {
                index = 0;
                if (!dev_mode) pass_by++;
            }
            redraw = true;
        }
#if defined(HAS_KEYBOARD) || defined(HAS_5_BUTTONS) || defined(USE_CARDKB2)
        auto moveMainMenuRow = [&](int direction) {
            if (tftHeight <= 90) return;

            int cols = (tftHeight > 90) ? 3 : 5;
            int rows = (opt + cols - 1) / cols;
            int targetRow = index / cols + direction;
            if (targetRow < 0) {
                targetRow = rows - 1;
            } else if (targetRow >= rows) {
                targetRow = 0;
            }

            int targetStart = targetRow * cols;
            int targetEnd = targetStart + cols;
            if (targetEnd > opt) targetEnd = opt;

            int currentCenter = menuItems[index].x + menuItems[index].w / 2;
            int nextIndex = targetStart;
            int bestDistance = 0x7fffffff;
            for (int i = targetStart; i < targetEnd; ++i) {
                int candidateCenter = menuItems[i].x + menuItems[i].w / 2;
                int distance = candidateCenter > currentCenter ? candidateCenter - currentCenter
                                                               : currentCenter - candidateCenter;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    nextIndex = i;
                }
            }

            index = nextIndex;
            pass_by = 0;
            redraw = true;
        };

        if (check(UpPress)) { moveMainMenuRow(-1); }
        if (check(DownPress)) { moveMainMenuRow(1); }
#endif

        // Select and run function
        if (check(SelPress)) {
            menuItems.at(index).action(); // Call the action associated with the selected menu item
            tft->drawPixel(0, 0, 0);
            tft->fillScreen(BGCOLOR);
            pass_by = 0;
            returnToMenu = false;
            redraw = true;
            goto END;
        }
        checkReboot();
#if defined(HAS_RESISTIVE_TOUCH)
        if (Serial.available() > 0) {
            String msg = Serial.readStringUntil('\n');
            msg.trim();

            if (msg == "calibrate") {
                launcherConsolePrintln("Starting calibration..");
                calibrateTouch();
            }
        }
#endif
    }

END:
    vTaskDelay(pdMS_TO_TICKS(10));
}

#else
void loop() { // Start SD card, If there's no SD Card installed, see if there's ssid saved on memory,
    RAM_LOG("headless-loop-start");
    launcherConsolePrint(
        "     _                            _               \n"
        "    | |                          | |              \n"
        "    | |     __ _ _   _ _ __   ___| |__   ___ _ __ \n"
        "    | |    / _` | | | | '_ \\ / __| '_ \\ / _ \\ '__|\n"
        "    | |___| (_| | |_| | | | | (__| | | |  __/ |   \n"
        "    |______\\__,_|\\__,_|_| |_|\\___|_| |_|\\___|_|   \n"
        "    ----------------------------------------------\n"

        "Welcome to Launcher, an ESP32 firmware where you can have\n"
        "a better control on what you are running on it.\n\n"
        "Now it will Start a web interface, where you can flash a new\n"
        "firmware on a dedicated partition, and swap it whenever you\n"
        "want using this Launcher.\n\n\n"
    );

    getConfigs();
    launcherConsolePrintln("Scanning networks...");
    std::vector<LauncherWifiAp> networks;
    int nets = launcherWifiScan(networks);
    bool mode_ap = true;

    if (sdcardMounted) {
        JsonObject setting = settings[0];
        JsonArray WifiList = setting["wifi"].as<JsonArray>();
        for (int i = 0; i < nets; i++) {
            String networkSsid = networks[i].ssid.c_str();
            for (auto wifientry : WifiList) {
                launcherConsolePrintf("Target: %s Network: %s\n", ssid.c_str(), networkSsid.c_str());
                if (networkSsid == wifientry["ssid"].as<String>()) {
                    ssid = wifientry["ssid"].as<String>();
                    pwd = wifientry["pwd"].as<String>();
                    int count = 0;
                    launcherConsolePrintf("Connecting to %s\n", ssid.c_str());
                    LauncherWifiConnectState connectState = LauncherWifiConnectState::Pending;
                    while (connectState != LauncherWifiConnectState::Connected) {
                        connectState = launcherWifiConnectStatus(ssid.c_str(), pwd.c_str(), 500);
                        if (connectState == LauncherWifiConnectState::Connected) break;
                        if (connectState == LauncherWifiConnectState::WrongPassword) {
                            launcherConsolePrintln("Wrong Password");
                            break;
                        }
                        vTaskDelay(pdTICKS_TO_MS(500));
#if LED > 0
                        launcherGpioWrite(LED, count & 1 ? LED_ON : (LED_ON ? LOW : HIGH)); // blink the LED
#endif
                        launcherConsolePrint(".");
                        count++;
                        if (connectState == LauncherWifiConnectState::Failed || count > 20) {
                            break; // stops trying this network, will try the others, if there are some other
                                   // with same SSID
                        }
                    }
                    if (!launcherWifiIsConnected()) { saveIntoNVS(); }
                }
            }
        }
    } else if (ssid != "") { // will try to connect to a saved network
        for (int i = 0; i < nets; i++) {
            String networkSsid = networks[i].ssid.c_str();
            launcherConsolePrintf("Target: %s Network: %s\n", ssid.c_str(), networkSsid.c_str());
            if (ssid == networkSsid) {
                launcherConsolePrintln("Network matches the SSID, starting connection\n");
                int count = 0;
                launcherConsolePrintf("Connecting to %s\n", ssid.c_str());
                LauncherWifiConnectState connectState = LauncherWifiConnectState::Pending;
                while (connectState != LauncherWifiConnectState::Connected) {
                    connectState = launcherWifiConnectStatus(ssid.c_str(), pwd.c_str(), 500);
                    if (connectState == LauncherWifiConnectState::Connected) break;
                    if (connectState == LauncherWifiConnectState::WrongPassword) {
                        launcherConsolePrintln("Wrong Password");
                        break;
                    }
                    vTaskDelay(pdTICKS_TO_MS(500));
#if LED > 0
                    launcherGpioWrite(LED, count & 1 ? LED_ON : (LED_ON ? LOW : HIGH)); // blink the LED
#endif
                    launcherConsolePrint(".");
                    count++;
                    if (connectState == LauncherWifiConnectState::Failed || count > 20) {
                        break; // stops trying this network, will try the others, if there are some other with
                               // same SSID, it can take quite sometime :/
                    }
                }
            }
        }
    } else {
        launcherConsolePrintln(
            "Couldn't find SD Card and SSID Saved,\n"
            "you can configure it on the WEB Ui,\n\n"
            "Starting the Launcher in Access point mode\n"
            "Connect into the following network\n"
            "with no other network (mobile data off and unplug wired connections)"
        );
    }

    // if there's no network information, open in Access Point Mode
    if (launcherWifiIsConnected()) mode_ap = false;

    startWebUi("", 0, mode_ap);

    // sorfware will keep trapped in startWebUi loop..
}
#endif
