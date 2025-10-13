#include <globals.h>

#include <EEPROM.h>
#include <HTTPClient.h>
#include <WiFi.h>
// #include <M5-HTTPUpdate.h>
#if defined(HEADLESS)
#include <VectorDisplay.h>
#else
#include <tft.h>
#endif
#include "esp_ota_ops.h"
#include <SD.h>
#include <SPIFFS.h>

#include "powerSave.h"
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// Public Globals
uint32_t MAX_SPIFFS = 0;
uint32_t MAX_APP = 0;
uint32_t MAX_FAT_vfs = 0;
uint32_t MAX_FAT_sys = 0;
uint16_t FGCOLOR = GREEN;
uint16_t ALCOLOR = RED;
uint16_t BGCOLOR = BLACK;
uint16_t odd_color = 0x30c5;
uint16_t even_color = 0x32e5;

#if defined(HEADLESS)
uint8_t _miso = 0;
uint8_t _mosi = 0;
uint8_t _sck = 0;
uint8_t _cs = 0;
#endif

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
TouchPoint touchPoint;
keyStroke KeyStroke;

#if defined(HAS_TOUCH)
volatile uint16_t tftHeight = TFT_WIDTH - (FM * LH + 4);
#else
volatile uint16_t tftHeight = TFT_WIDTH;
#endif
volatile uint16_t tftWidth = TFT_HEIGHT;
TaskHandle_t xHandle;
void __attribute__((weak)) taskInputHandler(void *parameter) {
    auto timer = millis();
    while (true) {
        checkPowerSaveTime();
        if (!AnyKeyPress || millis() - timer > 75) {
            resetGlobals();
#ifndef DONT_USE_INPUT_TASK
            InputHandler();
#endif
            timer = millis();
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
uint16_t total_firmware = 0;
uint8_t current_page = 1;
uint8_t num_pages = 0;
JsonDocument doc;
JsonArray favorite;
JsonDocument settings;
std::vector<Option> options;
const int bufSize = 1024;
uint8_t buff[1024] = {0};

#include "display.h"
#include "massStorage.h"
#include "mykeyboard.h"
#include "onlineLauncher.h"
#include "partitioner.h"
#include "sd_functions.h"
#include "settings.h"
#include "webInterface.h"

/*********************************************************************
**  Function: get_partition_sizes
**  Get the size of the partitions to be used when installing
*********************************************************************/
void get_partition_sizes() {
    // Obter a tabela de partições
    const esp_partition_t *partition;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);

    // Iterar sobre as partições do tipo APP
    while (it != NULL) {
        partition = esp_partition_get(it);
        if (partition != NULL && partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
            MAX_APP = partition->size;
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    // Iterar sobre as partições do tipo DATA
    it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        partition = esp_partition_get(it);
        if (partition != NULL) {
            if (partition->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS) {
                MAX_SPIFFS = partition->size;
            } else if (partition->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT) {
                log_i("label: %s", partition->label);
                if (strcmp(partition->label, "vfs") == 0) MAX_FAT_vfs = partition->size;
                else if (strcmp(partition->label, "sys") == 0) MAX_FAT_sys = partition->size;
            }
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    if (MAX_SPIFFS == 0 && askSpiffs) gsetAskSpiffs(true, false);

    // Logar os tamanhos das partições
    ESP_LOGI("Partition Sizes", "MAX_APP: %d", MAX_APP);
    ESP_LOGI("Partition Sizes", "MAX_SPIFFS: %d", MAX_SPIFFS);
    ESP_LOGI("Partition Sizes", "MAX_FAT_sys: %d", MAX_FAT_sys);
    ESP_LOGI("Partition Sizes", "MAX_FAT_vfs: %d", MAX_FAT_vfs);

    // #if defined(HEADLESS)
    //     // This piece of the code is supposed to handle the absence of APP partition tables
    //     // if it doent's find App partition, means that the ESP32 can be 4, 8 or 16Mb, that must be set in
    //     platformio.ini
    //     // and the partition size will be discovered using esp_flash_get_physical_size function
    //     uint32_t __size;  // Variable to store the flash size from flash ID
    //     //esp_err_t result = esp_flash_get_physical_size(NULL,&__size); // This guy reads the FlashID size,
    //     won't work for now, that is the true size of the chip
    //                                                                     // if flashing with a 4Mb
    //                                                                     bootloader and installing 8Mb
    //                                                                     partition scheme, it breakes
    //                                                                     (bootloop) probably in this
    //                                                                     function:
    //                                                                     //
    //                                                                     https://github.com/bmorcelli/esp-idf/blob/621a7fa1208ce44dbf1e0038c43587a1dc362319/components/spi_flash/esp_flash_spi_init.c#L282
    //                                                                     // If i set a chip size greater
    //                                                                     than the one I have, I fall in this
    //                                                                     error:
    //                                                                     //
    //                                                                     https://github.com/bmorcelli/esp-idf/blob/621a7fa1208ce44dbf1e0038c43587a1dc362319/components/spi_flash/esp_flash_spi_init.c#L317
    //                                                                     // maybe commenting it in a custom
    //                                                                     fw can solve it, but will add lots
    //                                                                     of issues due to size changings and
    //                                                                     sets in this same function

    //     esp_err_t result = esp_flash_get_size(NULL,&__size);
    //     if (result == ESP_OK) {
    //         Serial.print("Flash size: ");
    //         Serial.println(__size, HEX);  // Prints value in hex
    //     } else {
    //         Serial.print("Error: ");
    //         Serial.println(result);  // Prints the errors, if any
    //     }
    //     if(MAX_APP==0) {
    //       // Makes the arrangements

    //       //partitionSetter(def_part, sizeof(def_part));
    //       if(__size==0x400000) { partitionSetter(def_part, sizeof(def_part)); }
    //       if(__size==0x800000) { partitionSetter(def_part8, sizeof(def_part8));}
    //       if(__size==0x1000000) { partitionSetter(def_part16, sizeof(def_part16)); }
    //       while(1) {
    //         Serial.println("Turn Off and On again to apply partition changes.");
    //         delay(2500);
    //       }
    //     }

    //   #endif
}
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
#if CONFIG_IDF_TARGET_ESP32P4
    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    esp_ota_set_boot_partition(partition);
    EEPROM.begin(16);
    int init = EEPROM.read(6);
    if (init >= 1) { // restart com eeprom em 1
        EEPROM.write(6, 0);
        EEPROM.commit();
        EEPROM.end();
        ESP.restart();
    } else {
        EEPROM.write(6, 1);
        EEPROM.commit();
        EEPROM.end();
    }
#endif
    Serial.begin(115200);

// Setup GPIOs and stuff
#if defined(HEADLESS)
#if SEL_BTN >= 0 // handle enter in launcher
    pinMode(SEL_BTN, INPUT);
#endif
#if LED > 0
    pinMode(LED, OUTPUT); // Set pin to to recognize if launcher is starting or not, and connectiong or not
    digitalWrite(LED, LED_ON); // keeps on until exit
#endif

#endif
#if defined(BACKLIGHT)
    pinMode(BACKLIGHT, OUTPUT);
#endif

    _setup_gpio();

    EEPROM.begin(EEPROMSIZE + 32); // open eeprom.... 32 is the size of the SSID string stored at the end of
                                   // the memory, using this trick to not change all the addresses
    if (EEPROM.read(EEPROMSIZE - 13) > 3 || EEPROM.read(EEPROMSIZE - 14) > 240 ||
        EEPROM.read(EEPROMSIZE - 15) > 100 || EEPROM.read(EEPROMSIZE - 1) > 1 ||
        EEPROM.read(EEPROMSIZE - 2) > 1 ||
        (EEPROM.read(EEPROMSIZE - 3) == 0xFF && EEPROM.read(EEPROMSIZE - 4) == 0xFF &&
         EEPROM.read(EEPROMSIZE - 5) == 0xFF && EEPROM.read(EEPROMSIZE - 6) == 0xFF)) {
        log_i(
            "EEPROM back to default\n0=%d\n1=%d\n2=%d\n9=%d\nES-1=%d",
            EEPROM.read(EEPROMSIZE - 13),
            EEPROM.read(EEPROMSIZE - 14),
            EEPROM.read(EEPROMSIZE - 15),
            EEPROM.read(EEPROMSIZE - 1),
            EEPROM.read(EEPROMSIZE - 2)
        );
        EEPROM.write(EEPROMSIZE - 13, rotation); // Left rotation
        EEPROM.write(EEPROMSIZE - 14, 20);       // 20s Dimm time
        EEPROM.write(EEPROMSIZE - 15, 100);      // 100% brightness
        EEPROM.write(EEPROMSIZE - 1, 1);         // OnlyBins
        EEPROM.writeString(20, "");
        EEPROM.writeString(EEPROMSIZE, ""); // resets ssid at the end of the EEPROM
        EEPROM.write(EEPROMSIZE - 2, 1);    // AskSpiffs

        // FGCOLOR
        EEPROM.write(EEPROMSIZE - 3, 0x07);
        EEPROM.write(EEPROMSIZE - 4, 0xE0);
        // BGCOLOR
        EEPROM.write(EEPROMSIZE - 5, 0);
        EEPROM.write(EEPROMSIZE - 6, 0);
        // ALCOLOR
        EEPROM.write(EEPROMSIZE - 7, 0xF8);
        EEPROM.write(EEPROMSIZE - 8, 0x00);
        // odd
        EEPROM.write(EEPROMSIZE - 9, 0x30);
        EEPROM.write(EEPROMSIZE - 10, 0xC5);
        // even
        EEPROM.write(EEPROMSIZE - 11, 0x32);
        EEPROM.write(EEPROMSIZE - 12, 0xe5);

#if defined(HEADLESS)
        // SD Pins
        EEPROM.write(90, 0);
        EEPROM.write(91, 0);
        EEPROM.write(92, 0);
        EEPROM.write(93, 0);
#endif
        EEPROM.commit(); // Store data to EEPROM
    }

    rotation = EEPROM.read(EEPROMSIZE - 13);
    dimmerSet = EEPROM.read(EEPROMSIZE - 14);
    bright = EEPROM.read(EEPROMSIZE - 15);
    onlyBins = EEPROM.read(EEPROMSIZE - 1);
    askSpiffs = EEPROM.read(EEPROMSIZE - 2);
    FGCOLOR = (EEPROM.read(EEPROMSIZE - 3) << 8) | EEPROM.read(EEPROMSIZE - 4);
    BGCOLOR = (EEPROM.read(EEPROMSIZE - 5) << 8) | EEPROM.read(EEPROMSIZE - 6);
    ALCOLOR = (EEPROM.read(EEPROMSIZE - 7) << 8) | EEPROM.read(EEPROMSIZE - 8);
    odd_color = (EEPROM.read(EEPROMSIZE - 9) << 8) | EEPROM.read(EEPROMSIZE - 10);
    even_color = (EEPROM.read(EEPROMSIZE - 11) << 8) | EEPROM.read(EEPROMSIZE - 12);
    pwd = EEPROM.readString(20);          // read what is on EEPROM here for headless environment
    ssid = EEPROM.readString(EEPROMSIZE); // read what is on EEPROM here for headless environment

#if defined(HEADLESS)
                                          // SD Pins
    _miso = EEPROM.read(90);
    _mosi = EEPROM.read(91);
    _sck = EEPROM.read(92);
    _cs = EEPROM.read(93);
#endif

    EEPROM.end();

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

    // Performs the verification when Launcher is installed through OTA
    partitionCrawler();
    // Checks the size of partitions and take actions to find the best options (in HEADLESS environment)
    get_partition_sizes();
    // Checks if the fw in the OTA partition is valid. reading the firstByte looking for 0xE9
    const esp_partition_t *ota_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    uint8_t firstByte;
    esp_partition_read(ota_partition, 0, &firstByte, 1);
    // Gets the config.conf from SD Card and fill out the settings JSON
    getConfigs();
#if defined(HAS_TOUCH)
    TouchFooter2();
#endif

    _post_setup_gpio();

    // This task keeps running all the time, will never stop
    xTaskCreate(
        taskInputHandler, // Task function
        "InputHandler",   // Task Name
        3500,             // Stack size
        NULL,             // Task parameters
        2,                // Task priority (0 to 3), loopTask has priority 2.
        &xHandle          // Task handle (not used)
    );

    // Start Bootscreen timer
    int i = millis();
    int j = 0;
    LongPress = true;
    while (millis() < i + 5000) { // increased from 2500 to 5000
        initDisplay();            // Inicia o display

        if (millis() > (i + j * 500)) { // Serial message each ~500ms
            Serial.println("Press the button to enter the Launcher!");
            j++;
        }

        // Direct input check for startup - bypass check() function to avoid task suspension
        if (check(SelPress)) {
            tft->fillScreen(BGCOLOR);
            goto Launcher;
        }

#if defined(HAS_KEYBOARD)
        keyStroke key = _getKeyPress();
        if (key.pressed && !key.enter)
#elif defined(STICK_C_PLUS2) || defined(STICK_C_PLUS)
        if (NextPress)
#else
        if (check(AnyKeyPress))
#endif
        {
            tft->fillScreen(BLACK);
            FREE_TFT
#if CONFIG_IDF_TARGET_ESP32P4
            const esp_partition_t *partition =
                esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
            esp_ota_set_boot_partition(partition);
            ESP.deepSleep(100);
#endif
            ESP.restart();
        }
    }

    // If nothing is done, check if there are any app installed in the ota partition, if it does, restart
    // device to start installed App.
    if (firstByte == 0xE9) {
        tft->fillScreen(BLACK);
        FREE_TFT
#if CONFIG_IDF_TARGET_ESP32P4
        const esp_partition_t *partition =
            esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
        esp_ota_set_boot_partition(partition);
        ESP.deepSleep(100);
#endif
        ESP.restart();
    } else goto Launcher;

// If M5 or Enter button is pressed, continue from here
Launcher:
    LongPress = false;
    tft->fillScreen(BGCOLOR);
#if LED > 0 && defined(HEADLESS)
    digitalWrite(LED, LED_ON ? LOW : HIGH); // turn off the LED
#endif
}

/**********************************************************************
**  Function: loop
**  Main loop
**********************************************************************/
#ifndef HEADLESS
void loop() {
    bool redraw = true;
    bool update_sd;
    int index = 0;
    int opt = 5; // there are 3 options> 1 list SD files, 2 OTA, 3 USB and 4 Config
    int pass_by = 0;
    getBrightness();
    if (!sdcardMounted) index = 1; // if SD card is not present, paint SD square grey and auto select OTA
    std::vector<MenuOptions> menuItems = {
        {
#if TFT_HEIGHT < 135
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
#if TFT_HEIGHT < 135
         "WUI", "Start WebUI",
#else
            "WUI",
            "Start Web User Interface",
#endif
         [=]() { loopOptionsWebUi(); }
        },
#if defined(ARDUINO_USB_MODE) && !defined(ARDUINO_M5STACK_TAB5)
        {
#if TFT_HEIGHT < 135
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
                    delay(2000);
                }
            }, sdcardMounted
        },
#endif
        {
#if TFT_HEIGHT < 135
         "CFG", "Change Settings.",
#else
            "CFG",
            "Change Launcher Settings.",
#endif
         [=]() { settings_menu(); }
        }
    };
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
#ifdef E_PAPER_DISPLAY
            tft->display(false);
            vTaskDelay(pdTICKS_TO_MS(200));
#endif
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
                    break;
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

        // Select and run function
        if (check(SelPress)) {
            menuItems.at(index).action(); // Call the action associated with the selected menu item
            tft->drawPixel(0, 0, 0);
            tft->fillScreen(BGCOLOR);
            pass_by = 0;
            returnToMenu = false;
            redraw = true;
        }
    }
}

#else
void loop() {
    // Start SD card, If there's no SD Card installed, see if there's ssid saved on memory,
    Serial.print(
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
    Serial.println("Scanning networks...");
    int nets = WiFi.scanNetworks();
    bool mode_ap = true;

    if (sdcardMounted) {
        JsonObject setting = settings[0];
        JsonArray WifiList = setting["wifi"].as<JsonArray>();
        for (int i = 0; i < nets; i++) {
            for (auto wifientry : WifiList) {
                Serial.println("Target: " + ssid + " Network: " + WiFi.SSID(i));
                if (WiFi.SSID(i) == wifientry["ssid"].as<String>()) {
                    ssid = wifientry["ssid"].as<String>();
                    pwd = wifientry["pwd"].as<String>();
                    WiFi.begin(ssid, pwd);
                    int count = 0;
                    Serial.println("Connecting to " + ssid);
                    while (WiFi.status() != WL_CONNECTED) {
                        vTaskDelay(pdTICKS_TO_MS(500));
#if LED > 0
                        digitalWrite(LED, count & 1 ? LED_ON : (LED_ON ? LOW : HIGH)); // blink the LED
#endif
                        Serial.print(".");
                        count++;
                        if (count > 10) { // try for 5 seconds
                            break; // stops trying this network, will try the others, if there are some other
                                   // with same SSID
                        }
                    }
                    if (WiFi.status() != WL_CONNECTED) {
                        // saves last connected network into EEPROM
                        EEPROM.begin(EEPROMSIZE + 32);
                        EEPROM.writeString(20, pwd);
                        EEPROM.writeString(EEPROMSIZE, ssid);
                        EEPROM.end();
                    }
                }
            }
        }
    } else if (ssid != "") { // will try to connect to a saved network
        for (int i = 0; i < nets; i++) {
            Serial.println("Target: " + ssid + " Network: " + WiFi.SSID(i));
            if (ssid == WiFi.SSID(i)) {
                Serial.println("Network matches the SSID, starting connection\n");
                WiFi.begin(ssid, pwd);
                int count = 0;
                Serial.println("Connecting to " + ssid);
                while (WiFi.status() != WL_CONNECTED) {
                    vTaskDelay(pdTICKS_TO_MS(500));
#if LED > 0
                    digitalWrite(LED, count & 1 ? LED_ON : (LED_ON ? LOW : HIGH)); // blink the LED
#endif
                    Serial.print(".");
                    count++;
                    if (count > 6) { // try for 3 seconds
                        break; // stops trying this network, will try the others, if there are some other with
                               // same SSID, it can take quite sometime :/
                    }
                }
            }
        }
    } else {
        Serial.println(
            "Couldn't find SD Card and SSID Saved,\n"
            "you can configure it on the WEB Ui,\n\n"
            "Starting the Launcher in Access point mode\n"
            "Connect into the following network\n"
            "with no other network (mobile data off and unplug wired connections)"
        );
    }

    // if there's no network information, open in Access Point Mode
    if (WiFi.status() == WL_CONNECTED) mode_ap = false;

    startWebUi("", 0, mode_ap);

    // sorfware will keep trapped in startWebUi loop..
}
#endif
