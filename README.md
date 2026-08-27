# Launcher
Application launcher for M5Stack, Lilygo, CYD, Marauder, and ESP32 devices.


<p align="center" width="100%">
    <img src="https://github.com/bmorcelli/Launcher/blob/main/M5Launcher.png?raw=true"> <img src="https://github.com/bmorcelli/Launcher/blob/main/New Launcher.jpg?raw=true" width="240" height="135"> <img src="https://github.com/bmorcelli/Launcher/blob/main/Launcher_anim.webp?raw=true" width="auto" height="135">
</p>

Join our [Discord community](https://discord.gg/BE9by2a2FF)

## How to Install
* Use the Flasher: [Launcher Flasher](https://bmorcelli.github.io/Launcher/).
* Use M5Burner, or
* Download the .bin file from Releases for your device and use https://web.esphome.io/ or esptool.py to flash the file `Launcher-{YourDevice}.bin` to your device.

## How to Use
* Turn your device on
* Press M5/Sel (Enter) on the Launcher start screen to get into Launcher
* Choose OTA to install new binaries from online services (M5Burner or GitHub links)
* After installation, when you turn on the device, the installed program will launch if you don't press anything.

## My SD Card doesn't work!
* Make sure your SD card is SDHC (not SDXC)
* Use a maximum size of 32GB (I use 8 or 16GB)
* Format it as FAT32 (use [Rufus](https://rufus.ie/) to format your SD card)
* Ensure the partition scheme is MBR, not GPT

## With Launcher you'll be able to:
<details>
  <summary><h3>OTA - OTA Update</h3></summary>

- Install binaries from online repositories (M5Burner or GitHub links) without needing a USB cable
- Install binaries from a WebUI that you can start from the WUI option, using binaries you have on your computer or smartphone
- Install binaries from your SD card

</details>
<details>
  <summary><h3>SD - SD Card Management</h3></summary>
- Create new folders,
- Delete files and folders,
- Rename files,
- Copy and paste files,
- Install binaries

</details>
<details>
  <summary><h3>WUI - Web User Interface</h3></summary>
- Manage files on the SD Card
- Install binaries wirelessly using the OTA Update option
- Edit text files
- Edit NVS information (UiFlow2 data, Launcher settings, and others)
- Deploy installations from the file list

</details>
<details>
  <summary><h3>CFG - Configurations (Customization)</h3></summary>
- Charge Mode
- Change brightness
- Change dim time
- Change UI color
- Avoid/Ask SPIFFS (change whether Launcher asks to install the SPIFFS file system; only Orca One uses this feature)
- Change rotation
- All files/Only Bins (see all files or only .bins - default)
- Change partition scheme (allows installing large apps or UiFlow2, for example)
- List of partitions
- Clear the FAT partition
- Save SPIFFS (save a copy of the SPIFFS partition to restore when needed)
- Restore SPIFFS
</details>

<details>
  <summary><h3>PMan - Partition Manager</h3></summary>
- View current partition scheme
- Create partitions
- Delete partitions
- Format partitions
- Back up data partitions (SPIFFS or FAT)
- Restore data partitions (SPIFFS or FAT)
- Resize partitions
</details>

<details>
  <summary><h3>Tips</h3></summary>
* Having an SD card gives you a better experience, but it is not required. [SD Card Hat for M5StickCs](https://www.thingiverse.com/thing:6459069)
* You can learn more about how it works on the [Launcher Wiki](https://github.com/bmorcelli/Launcher/wiki/Explaining-the-project).
* Where/how do I find binaries to launch? -> [Obtaining binaries to launch](https://github.com/bmorcelli/Launcher/wiki/Obtaining-binaries-to-launch)
* Now you can download binaries from [HERE!](https://bmorcelli.github.io/Launcher/catalog.html)
</details>

## Known Issues
* UiFlow 1 doesn't work with Launcher. It uses an old MicroPython distro, which uses an old ESP-IDF distro with lots of secrets that I couldn't figure out.

## To-Do list
Things that need to be done in future updates

* [ ] LVGL UI (?)
* [ ] Move to ESP-IDF Platform

## Latest Changelog
* 2.9.0:
     * [x] Port to [M5-Paper](https://shop.m5stack.com/products/m5paper-esp32-development-kit-v1-1-960x540-4-7-eink-display-235-ppi?ref=Pirata)
     * [x] Port to [M5-Paper Mono](https://shop.m5stack.com/products/m5papermono-with-lora-nfc-800x480-3-97-eink-display?ref=Pirata)
     * [x] Port to [M5-Paper Color](https://shop.m5stack.com/products/m5paper-color-esp32s3-dev-kit?ref=Pirata) - Different workflow, Prints on scren but works onl through WebUI. Display takes long time to render, making navigation almost impossible for Launcher.
     * [x] Port to [T-Display P4](https://lilygo.cc/products/t-display-p4?bg_ref=sDI8Bh4HmO)
     * [x] Port to [T-Display C5](https://lilygo.cc/products/t-display-c5?bg_ref=sDI8Bh4HmO)
     * [x] Port to [ESP32-S3 ES3C28P 2.8"](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display) aka `Cheap Black Display`, thanks to @AlexeOnTheShip
     * [x] Port to [Elecrow Crowpanel Advance ESP32-S3 5.0in](https://www.elecrow.com/crowpanel-advance-5-0-hmi-esp32-ai-display-800x480-ips-artificial-intelligent-touch-screen.html)
     * [x] Port to [Elecrow Crowpanel Advance ESP32-P4 7.0in](https://www.elecrow.com/crowpanel-advanced-7inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-with-wifi-6-compatible-with-arduino-lvgl-micropython.html)
     * [x] Port to [ESP32 E32R40T 4"](https://www.lcdwiki.com/4.0inch_ESP32-32E_Display)
     * [x] Port to [XTeink X3](https://www.xteink.com/products/xteink-x3)
     * [x] **BETA**: Port to [XTeink X4 Pro](https://www.xteink.com/products/xteink-x4-pro-pocket-ereader) - Need validating touchscreen and display
     * [x] **BETA**: Port to [Waveshare-ESP32-C5-LCD-1.47](https://www.waveshare.com/wiki/ESP32-C5-LCD-1.47)
     * [x] **BETA**: Port to [Waveshare-ESP32-C6-LCD-1.47](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47)
     * [x] **BETA**: Port to [Waveshare-ESP32-C6-LCD-1.3](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47)
     * [x] **BETA**: Port to [SeeedStudio ReTerminal Sticky](https://www.seeedstudio.com/reTerminal-Sticky-p-6861.html)
     * [x] **ATENTION**: Arduino **Nesso N1** and **M5Stack StickS3** Navigation has changed, Front button short press to move Next and Long press to Select, Side button short press to mode Previous an long press to Escape.. on Keyboard, holding Escape will change the moviment from Left/Up to Right/Down, so typing will be easier to handle (after you master it).
     * [x] **Added** option to opt-out from Automatically connect to a known Network.
     * [x] **Changed** brightness control, now it uses a slider bar with 5% steps.
     * [x] Moved display drivers handling to [DisplayDrivers](https://github.com/bmorcelli/DisplayDrivers) lib, using its superclass to handle all display drivers used by Launcher. This removed `tft.h`/`tft_inits.h` and the Arduino_GFX DSI patch scripts from the project.
     * [x] Reorganized boards files to make porting easier. It now needs only 2 files and add it to CI/CD. Per-board JSON files were replaced by one per chip family (`esp32`, `esp32s2`, `esp32s3`, `esp32c3`, `esp32c5`, `esp32c6`, `esp32p4`), and envs that only differ by pinout were merged (`m5stack-plus`, `lilygo-t-embed-all`, `xteink-x3-x4`).
     * [x] Removed `phy_init` partition and increased NVS to 0x5000
     * [x] Centralized NVS code handlers, and added a `nimble` compatibility layer between firmwares. Now older and newer versions of nimble_bonds are saved per device (it happens only when using BLE to connect to a device, creating bound data) and it is switched and restored when changing firmware in runtime. relate to this [issue](https://github.com/bmorcelli/Launcher/issues/400)
     * [x] Devices are now described by `HAS_2_BUTTONS`/`HAS_6_BUTTONS` and long-press navigation, replacing the old per-board button workarounds.
     * [x] Fixed original M5Paper display and SD bus, thanks to @Decentricity
     * [x] Fixed Marauder v6.1 hypersensitive touch and SPI noise, thanks to @H4W9
     * [x] Fixed T-Display P4 install from SD Card and the SD/display bus conflict
     * [x] CI/CD now cancels superseded runs, retries flaky PlatformIO builds and caches the PlatformIO core; added a `clang-format` script and `.gitattributes` to keep the source formatted.


* 2.8.0:
     * [x] Port to Xueersi-XiaoMiao
     * [x] Lilygo T-Watch-Ultra now has OTA installing
     * [x] **Shortcut system for Keyboard devices**: Devices with Keyboard can bind a binary to a key, and install it with the press of one key from Main menu
     * [x] **Fastboot for Keyboard and Touchscreen devices**: On bootscreen you can press a key or touch the screen to fast boot into an installed firmware
     * [x] **Added Serial interface**: type "help" on serial to check available commands, you can configure Wifi from there, useful for devices like StickCPlus2 and others without keyboard or touchscreen: e.g. "wifi add MyNetworkSSID MyN37w0rkPWD", calibrate touchscreen, mirror X/Y axis or swap XY axis.
     * [x] **Downloaded firmware update list**. Now launcher can check the firmware you downloaded, stored at `{dwn_path}/downloaded.json` and check if there are new versions of these firmwares, and it can download all new files directly to your SDCard if you hit `[Update All]`.
     * [x] **Multi-part binary download**: Firmware composed by multiple files (bootloader.bin, partitions.bin, firmware.bin and data.bin) available on LauncherHub are now downloaded and merged at runtime. It fetches data, one by one, and saves them into on single file to be flashed from the SDCard.
     * [x] **New Data partition Backup system**: Now it saves the relationship between backup and binary on `/bkp/backupData.json` and restores it when reinstalling. App icon will display backup data, and PMan too. Needs SD Card to use this feature.
     * [x] **WebUI** now shows partitioning info and allows partition management.
     * [x] **Added support to M5 CardKB2, CardKB 1.1 and Tab5 keyboard**. Support was added to M5 devices using grove I2C Port A for: StickC, StickCPlus 1.1, StickCPlus2, StickS3, Tab5, CoreS3, Core2 and Core, Thanks to @cddigi
     * [x] Fixed unheld traffic on TSL 1.3 networks with proper error handling, thanks to @anton-vinogradov
     * [x] Data partitions are now created with the label set in the partition table, and added compatibility to xiaozhi-esp32, that needs more SPIFFS storage, thanks to @QQSHI13
     * [x] Firmware containing valid data partitions now have their data partition copied as is. Empty partitions are created with minimum size of 0x70000 (448kb) or filling the flash if passes the threshold size.
     * [x] FAT labeled "sys" and "system" are now copied entirely, and the other partitions (FAT or LittleFS) are properly copied and resized to avoid being re-formatted, it gives compatibility to UiFlow, Tactility and some other firmware.
     * [x] E-paper displays now show "Powered Off" when turned off by launcher.
     * [x] Optimized RAM, reducing Heap fragmentation
     * [x] Fix M5Stack PaperS3 touchscreen https://github.com/bmorcelli/Launcher/issues/338
     * [x] Fix WebUI OTA installation
     * [x] Added "dev" option to Reset Config and wifi credentials

<details>
  <summary><h2>Older Changelogs</h2></summary>

* 2.7.2:
     * [x] Adjusted boot logic, now it works as it was before.
     * [x] Fixed devices that share TFT and Touch SPI bus (NM-CYD-C5 and others), that were unable to install firmware from SD Card.
     * [x] Fixed devices that share TFT with SD Card being unable to download firmware from OTA.

* 2.7.1:
     * [x] Fixed Launcher auto updater process.

* 2.7.0:
     * [x] Improved navigation on devices with 5 buttons and keyboards; now you can navigate up and down on the main menu
     * [x] Improved main menu for small screens (T-Dongle, StickC)
     * [x] Custom Partition Manager "PMan" (no more hardcoded partitions on Cardputer)
     * [x] Automatic partition maker, firmware sanity checker, and flash optimizer.
     * [x] Ability to install more than one firmware at a time
     * [x] Encrypted WiFi passwords within config.conf and NVS
     * [x] Remodeled WebUI, adding the ability to edit text files and NVS fields saved in memory
     * [x] Changed items on CFG for better understanding
     * [x] Optimized WiFi/WebUI functions to save Flash
     * [x] Enabled OTA for CYD-3248S035C and CYD-3248S035R
     * [x] Port to [M5-DinMeter](https://shop.m5stack.com/products/m5stack-din-meter-w-m5stamps3?ref=Pirata)
     * [x] Port to [Arduino Nesso N1](https://docs.arduino.cc/hardware/nesso-n1)
     * [x] Port to [Reaper Board](https://www.elecrow.com/bruce-pcb-rf-reaper.html)

* 2.6.10:
     * [x] Fixed navigation on 3-button devices that were triggering EscPress when pressing Next or Prev buttons.

* 2.6.9:
     * [X] Port to [NM-CYD-C5](https://www.nmminer.com/product/nm-cyd-c5/)
     * [x] Port to [3248W535C](https://www.displaysmodule.com/sale-51335661-jc3248w535c-i-y-lcd-display-module-with-320-480-pixel-resolution-and-effective-display-area-73-4-49-.html)
     * [x] Port to [Waveshare-ESP32-S3-LCD-1.47](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47)
     * [x] Port to [T-Dongle-C5 (tft)](https://lilygo.cc/products/t-dongle-c5?bg_ref=sDI8Bh4HmO)
     * [x] Port to [T-Display S3 AMOLED Plus](https://lilygo.cc/products/t-display-s3-amoled-plus)
     * [x] Fix issue where brightness wasn't being saved
     * [x] Fix issue where custom "downloads" folder wasn't being used
     * [x] Fix issue where some firmware downloads weren't working due to prohibited characters in firmware names
     * [x] Fixed SD card not mounting on T-Watch Ultra and adjusted UI for rounded edges on the main menu and file listings
     * [x] Enabled WASD navigation for Lilygo T-LoraPager, T-Deck, T-Deck Plus, and T-Deck Pro
     * [x] Added touch calibration for XPT2046-driven devices (resistive touchscreens, like CYD, Marauder, Phantom, and T-HMI). It will be prompted when no calibration data is found (first boot), or can be started from `CFG > Calibrate Touch` or by sending "calibrate" over Serial.
     * [x] Added an "OFF" option to the main screen to turn off the device (DeepSleep when PMIC is not available).
     * [x] Centralized main libraries using git submodules, so it doesn't download many copies of the same library to the `.pio/libdeps/**/` folder
     * [x] Speed up workflow using GitHub cache for framework and libs

* 2.6.8:
     * [x] Increased Game Station partition sizes. https://github.com/bmorcelli/Launcher/issues/299
     * [x] Added an option to erase the app partition
     * [x] Added an option to always boot into Launcher https://github.com/bmorcelli/Launcher/issues/292
     * [x] Added "APP" menu item to reboot into the last installed firmware
     * [x] Enhanced NVS partition and partition schemes for UiFlow2
     * [x] Fix M5Stack Tab5 WiFi scan and WebUI in AP mode.
     * [x] New Device: [Lilygo T-Watch-S3](https://lilygo.cc/products/t-watch-s3?bg_ref=sDI8Bh4HmO)
     * [x] New Device: [Lilygo T-Watch-Ultra](https://lilygo.cc/products/t-watch-ultra?bg_ref=sDI8Bh4HmO)
     * [x] New Device: [Lilygo E-Paper S3 Pro H752-x](https://lilygo.cc/products/t5-e-paper-s3-pro?bg_ref=sDI8Bh4HmO)


* 2.6.7:
     * [x] Re-established Tab5 SD card on SPI for WiFi compatibility and refactored the reboot process to power cycle the SD card, resetting the SD card communication bus.
     * [x] Tab5 now has a Mass Storage interface!
     * [x] Fixed M5Stack Paper S3 USB Mass Storage issue https://github.com/bmorcelli/Launcher/issues/287 and OTA will show only S3 firmware.
     * [x] Added app offset parameter to allow Launcher to be updated in Tab5, and firmware that is placed at different factory/app0 addresses.
     * [x] Set StickC and CPlus devices to see only ESP32 firmware on OTA, excluding ESP32-S3 (StickS3) firmware
     * [x] Adjusted firmware binaries that are not merged to have a partition table, allowing a SPIFFS partition to be attached to them. This binary will be valid ONLY for Launcher; it can't be flashed alone because it doesn't have a valid bootloader and partition table. https://github.com/bmorcelli/Launcher/issues/289

* 2.6.6:
     * [x] [M5-StickS3](https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit?ref=Pirata) port
     * [ ] (Beta) [M5-DinMeter](https://shop.m5stack.com/products/m5stack-din-meter-w-m5stamps3?ref=Pirata) port
     * [x] [Arduino Nesso N1](https://docs.arduino.cc/hardware/nesso-n1) port

* 2.6.5:
     * [x] M5-Tab5 using SDMMC driver for compatibility with other firmware
     * [x] Added the ability to order by "Latest update"
     * [x] Port to OpenSourceSRDLabs [WaveSentry and WaveSentry Pro ](https://opensourcesdrlab.com/products/aifw-wavesentry-esp32?VariantsId=10331)
     * [x] Battery ADC measurement fix for Cardputer, Tdeck, StickCPlus2, T-Display S3, T-HMI

* 2.6.4:
     * [x] Fixed CYD 3243S035R touchscreen rotation
     * [x] Fixed Marauder V7 screen issues
     * [x] Enhanced M5-PaperS3 display, using Sprites now for better drawing resolution
     * [x] Fixed M5Stack Cardputer ADV keyboard not adding '*' and '('
     * [x] Add new partition scheme for [Cardputer Game Station](https://github.com/geo-tp/Cardputer-Game-Station-Emulators/), allowing 4.5MB game ROMs
     * [ ] Port to [M5-Paper](https://shop.m5stack.com/products/m5paper-esp32-development-kit-v1-1-960x540-4-7-eink-display-235-ppi?ref=Pirata)
     * [ ] Port to [Arduino Nesso N1](https://docs.arduino.cc/hardware/nesso-n1), not building yet

* 2.6.3:
     * [x] Fixed Marauder keyboard
     * [x] Changed from EEPROM to NVS to save configs and WiFi credentials [Issue 232](https://github.com/bmorcelli/Launcher/issues/232)
     * [x] Port to [M5-PaperS3](https://shop.m5stack.com/products/m5papers3-esp32s3-development-kit?ref=Pirata)
     * [x] Fixed SelPress leaking into main menu
     * [x] T-Embed (all) and T-LoraPager Encoder enhancement
     * [x] WebUi session enhancement and fixes


* 2.6.2:
     * [x] Fixed T-Embed CC1101 OTA Link
     * [x] Changed SD file listings for speed([PR 230](https://github.com/bmorcelli/Launcher/pull/230)) [Issue 229](https://github.com/bmorcelli/Launcher/issues/229) thanks @geo-tp and @emericklaw

* 2.6.1:
     * [x] Fixed T-Deck Plus touchscreen (added new env for it)
     * [x] Fixed M5Stack CoreS3 SD Card not mounting
     * [x] Add Delete from Favorites option

* 2.6.0:
     * [x] Pulling data from my new API, integrating all device lists with the M5Burner API, and counting downloads in the M5Burner database.
     * [x] OTA Pagination (pages of 100 firmware ordered by download-default)
     * [x] New Device: [M5Stack Tab5](https://shop.m5stack.com/products/m5stack-tab5-iot-development-kit-esp32-p4?ref=Pirata).
     * [x] New Device: [Lilygo T-HMI](https://www.lilygo.cc/products/t-hmi?bg_ref=sDI8Bh4HmO)
     * [x] New Feature: Backup SPIFFS/FAT now prompts to merge data into a chosen binary, so you can install firmware and data by choosing `SPIFFS Yes` during install, and backup binaries now have incremental names.
     * [x] New Feature: Added filter and ordering to the firmware list.
     * [x] New Feature: Added a *Starred* firmware list (controlled by me; support the project to have your firmware added to the starred list.).
     * [x] New Feature: Added "Favorite" (requires SD card), where you can add firmware from the OTA list or manually add binary links from your local server or elsewhere.
     * [x] config.conf changes:
```
...
    "favorite": [
      { // Example of firmware added into Favorites through OTA function
        "name": "Evil-Cardputer-7h30th3r0n3",
        "fid": "2128851a0c98a4c1d15ac1a327b49812",
        "link": ""
      },
      { // Example of my custom link file added by manually editing this file
        "name": "Launcher Beta link",
        "fid": "", // leave it blank
        "link": "https://github.com/bmorcelli/Launcher/releases/download/beta/Launcher-m5stack-cardputer.bin"
      },
      {
        "name": "Bruce Beta link",
        "fid": "",
        "link": "https://github.com/pr3y/Bruce/releases/download/betaRelease/Bruce-m5stack-cardputer.bin"
      },
    ],
    "c0:4e:30:13:8d:f4": 1, // Rotation is now bound to hardware MAC
...

```

* 2.5.3:
     * [x] Restored T-Deck OTA
     * [x] Refined T-Deck Touchscreen inputs

* 2.5.2:
     * [x] Fixed Marauder V6 touchscreen and CYDs touchscreen unresponsive [issue](https://github.com/bmorcelli/Launcher/issues/210) and fixed Dim screen
     * [x] Fixed Marauder Mini and V7 screen dimming.
     * [x] Fixed Smoochiee board inputs and SD card [issue](https://github.com/bmorcelli/Launcher/issues/209)
     * [x] Fixed Longpress on Cardputer ADV
     * [x] Fixed folder creation on subfolders

* 2.5.1:
     * [x] Fixed Cardputer ADV Keyboard compatibility
     * [x] Fixed issue where OTA firmware list wasn't being fully downloaded.

* 2.5.0:
     * [x] Moving to pioarduino 3.3 based framework (ESP-IDF 5.5)
          * [x] Enable USB Mass Storage to SD_MMC devices (T-Display-S3 and touch and T-Dongle S3 tft)
          * [x] Partition changes
          * [x] Firmware updates
          * [x] OTA lists and installation
     * [x] Added keyboard support to T-Deck Pro https://github.com/bmorcelli/Launcher/issues/180
     * [x] Fixed compatibility with UIFlow 2.3.x https://github.com/bmorcelli/Launcher/issues/192
     * [x] Added CSS, JS, Html online minifier
     * [x] Moved to ESP32Async/ESPAsyncWebServer official repo
     * [x] Port to [Cardputer ADV](https://shop.m5stack.com/products/m5stack-cardputer-adv-version-esp32-s3?ref=Pirata) thanks to [@n0xa](https://github.com/n0xa)
     * [x] Port to [Lilygo Lora Pager](https://lilygo.cc/products/t-lora-pager) by @emericklaw
     * [x] Port to [CYD-4827S043R](https://github.com/bmorcelli/Launcher/issues/186) -> WIP
     * [x] Fixed first line [filelist](https://github.com/bmorcelli/Launcher/issues/166)

* 2.4.10:
     * [x] Fixed T-Embed screen
     * [x] Fixed StickC (and plus) keyboard navigation
     * [x] Phantom touchscreen mapping

* 2.4.9:
     * [x] Fixed T-Display-S3 PRO
     * [x] Enabled OTA for Marauder Mini

* 2.4.8:
     * [x] Enabled OTA function to: CYD 2432S028R, 2-USB, S024R, W328C/R, Marauder boards, Awok boards, Phantom, Lilygo T-Embed CC1101 and T-Deck (regular and plus)
     * [x] Port to [AWOK Mini v2](https://awokdynamics.com/products/dual-mini-v2) and [AWOK Touch v2](https://awokdynamics.com/products/dual-touch-v2)
     * [x] Port to [RabbitLabs Phantom](https://rabbit-labs.com/product/the-phantom-by-rabbit-labs/?v=dc634e207282)
     * [x] Ports to [8048S043C, 8048W550C](https://github.com/bmorcelli/Launcher/issues/108)
     * [x] Port to Lilygo T-Deck Pro (e-paper display)
     * [x] Enhancements on Touchscreen devices for responsive file lists and menu options
     * [x] New Main menu with all items, with items touchable.
     * [x] fixed ports to [Marauder v4, v6, v7, mini.](https://github.com/bmorcelli/Launcher/issues/146)
     * [x] fix for [2432s032C misaligned touchscreen ](https://github.com/bmorcelli/Launcher/issues/149)

* 2.4.7:
     * [x] WebUi: Multi file upload through drag/drop or file/folder selector, now it supports folder upload, and sorting.
     * [x] StickC blackscreen fix
     * [x] [T-Dongle-S3 (tft)](https://lilygo.cc/products/t-dongle-s3?srsltid=AfmBOopwCcPQTTC4wTNi3rNZHn8W6g8Yo_ShcrfDiAfECS6tGq59vWo7) port
     * [x] [T-Display-S3](https://lilygo.cc/products/t-display-s3?variant=42284559827125) port
* 2.4.6:
     * [x] UiFlow2 v2.2.3 and restored StickCPlus2 compatibility
     * [x] split webui files
     * [x] USB Interface to manage SD files on ESP32S3 devices (ESP32 can't do it)
     * [x] Compressed WebUI with gzip
     * [x] Port to CYD-3248S035C and CYD-3248S035R https://github.com/bmorcelli/Launcher/issues/125
     * [x] Interfaces skipping options (multiple clicks) https://github.com/bmorcelli/Launcher/issues/127 https://github.com/bmorcelli/Launcher/issues/126 [comment](https://github.com/bmorcelli/Launcher/issues/125#issuecomment-2705628306)
     * [x] Fixed StickCPlus keyboard colors
     * [x] Fixed Back to list on OTA (will be enhanced when having multiple lists)
     * [x] (rollback) Use http download/update for OTA to reduce flash memory.
* 2.4.5:
     * [x] Port to CYD-2432S024R https://github.com/bmorcelli/Launcher/issues/99 , CYD-2432W328R, CYD-2432S022C https://github.com/bmorcelli/Launcher/issues/112 , CYD-2432S032C, CYD-2432S032R
     * [x] Fixed Marauder V4-OG device
     * [x] Removed Battery indication when it is not available (or 0%)
     * [x] Fixed Headless 16Mb environment  https://github.com/bmorcelli/Launcher/issues/121 https://github.com/bmorcelli/Launcher/issues/120
     * [x] Now using ArduinoGFX as main graphics lib, with support to TFT_eSPI and LovyanGFX
* 2.4.4:
     * [x] Disabled OTA menu for non M5 Stack Devices (save flash memory for CYD and Marauder, mostly), creating a new partition scheme for these devices
     * [x] Fixed T-Embed CC1101 battery value

* 2.4.3:
     * [x] Fixed buttons on Core devices
     * [x] Fixed random restarts when dimming screen
     * [x] Ported to Lilygo E-Paper S3 Pro (Only Pro for now)
     * [x] Fixed T-Embed return from deepSleep

* 2.4.2:
     * [x] UiFlow2 v2.2.0 compatibility https://github.com/bmorcelli/Launcher/issues/92 for Cardputer, Removed from StickCPlus2 due to lack of storage
     * [x] Fix for https://github.com/bmorcelli/Launcher/issues/93 https://github.com/bmorcelli/Launcher/issues/97 https://github.com/bmorcelli/Launcher/issues/95
     * [x] Possibility to connect to Hidden Networks https://github.com/bmorcelli/Launcher/issues/89 by typing the SSID and Pwd
     * [x] Changed porting system, reading inputs on a background task (same as Bruce)
     * [x] Enhanced Keyboard
     * [x] Added Portrait rotation for bigger screens (bigger than 200x200px, such as CYD, Core devices)

* 2.4.1:
     * [x] T-Deck SD Card fix (Disable LoRa Chip, CS pin to High state, to avoid conflicts) https://github.com/bmorcelli/Launcher/issues/86
     * [x] Lilygo T-Display-S3-Pro port https://github.com/bmorcelli/Launcher/issues/73

* 2.4.0:
     * [x] CYD-2432W328C port https://github.com/bmorcelli/Launcher/issues/80
     * [x] Rolling texts for large SSIDs and large filenames
     * [x] Added ways to return from menu after wrong WIFI passwords and other menus (Exit from keyboard itself won't be available) https://github.com/bmorcelli/Launcher/issues/82 https://github.com/bmorcelli/Launcher/issues/81
     * [x] Fixed Orientation issues (not saving in the SD Card) https://github.com/bmorcelli/Launcher/issues/84
     * [x] Dim Screen now turns the screen off
     * [x] Renamed project to "Launcher" and add my nickname in the boot animation
     * [x] Changed interfacing code, preparing for new ports https://github.com/bmorcelli/Launcher/issues/83
* 2.3.2:
     * [x] T-Embed CC1101 power chip management fix
* 2.3.1:
     * [x] Fox for https://github.com/bmorcelli/Launcher/issues/77
     * [x] Fixed screen direction for T-Deck devices
     * [x] Fixed Json handling and config.conf random fails
* 2.3.0:
     * [x] Ported to Lilygo T-Embed CC1101
     * [x] Ported to Lilygo T-Embed
     * [x] Ported to Lilygo T-Deck
     * [x] Headless version for ESP32 and ESP32-S3
     * [x] StickCs Power Btn and Prev Btn now act as up controls in menus; long press to exit menu
* 2.2.5:
     * [x] Changed framework to remove all Watchdog Timers https://github.com/bmorcelli/Launcher/issues/61 https://github.com/bmorcelli/Launcher/issues/63 and solve some SD related issues
     * [x] Set Grove pins to LOW state on StickCs and Cardputer to avoid 433Mhz jamming while RF433T is connected
* 2.2.4:
     * [x] Finally ported to CoreS3 and CoreS3-SE
     * [x] Added reset to watchdog on WebUI and check for free memory when loading files to WebUI, to avoid crashes.
* 2.2.3:
     * [x] StickC, Plus and Plus2: removed power btn from skip logic at start.
     * [x] Changed EEPROM addresses to avoid problems with the new Bruce and other firmware.
* 2.2.2:
     * [x] Port for [CYD-2432S028R](https://www.amazon.com/dp/B0BVFXR313) and [CYD-2-Usb](https://www.amazon.com/dp/B0CLR7MQ91)
     * [x] Added a check after downloads finish that will delete failed downloaded files
* 2.2.1:
     * [x] Port for [Lilygo T-Display S3 Touch](https://www.lilygo.cc/products/t-display-s3?variant=42351558590645)
     * [x] Fixed JSON read/write
* 2.2.0:
     * [x] M5Launcher 2.2+ can now be updated over the air or using an SD card
     * [x] UI color settings (can be customized in /config.conf file)
     * [x] Reduced flickering on SD files navigation
     * [x] Fixed problem where firmware names with "/" prevented downloads
     * [x] Appended firmware version to the download name.
     * [x] Added Dim time to lower brightness and CPU freq while idle
     * [x] Added "Charge Mode" in settings, reducing CPU frequency to 80MHz and brightness to 5%, https://github.com/bmorcelli/Launcher/issues/40
     * [x] Fixed download progress bar https://github.com/bmorcelli/Launcher/issues/41
     * [x] Change default folder for download (manually on /config.conf) https://github.com/bmorcelli/Launcher/issues/15
     * [x] Save more SSIDs and passwords, and connect automatically if it is a known network (config.conf) https://github.com/bmorcelli/Launcher/issues/30
     * [x] Slightly increased WiFi download/OTA speed using a customized framework.
* 2.1.2:
     * [x] Fixed OTA error message
     * [x] Increased Options Menu width and reduced menu flickering
* 2.1.1:
     * [x] Fixed UIFlow Compatibility
     * [x] Fixed SD card issues
     * [x] Small Fixes https://github.com/bmorcelli/Launcher/issues/37
* 2.1.0:
     * [x] Core Fire (all 16Mb Core devices) and Core2 compatibility
     * [x] Made SPIFFS update optional (turned off by default) (config.conf)
     * [x] De-Sprite-fied the screens for Core devices (no PSRAM, unable to handle huge sprites) https://github.com/bmorcelli/Launcher/issues/34
     * [x] Make keyboard work with touchscreen capture in Core devices
     * [x] Dedicated btn for WebUI on main screen https://github.com/bmorcelli/Launcher/issues/22
     * [x] Multiple files upload on WebUI https://github.com/bmorcelli/Launcher/issues/28
     * [x] Update FAT VFS partition to make it compatible with UIFlow2 https://github.com/bmorcelli/Launcher/issues/29
     * [x] Partition changer to allow running DOOM and UIFlow on Cardputer and StickC
     * [x] Fixed (increased number of files) https://github.com/bmorcelli/Launcher/issues/33
     * [x] Backup and restore FAT and SPIFFS filesystems. If you use UIFlow, you can save all sketches to your device and make a backup to restore after reinstalling UIFlow, MicroHydra, or CircuitPython
* 2.0.1:
     * Fixed UIFlow installation https://github.com/bmorcelli/Launcher/issues/20
     * Fixed folder creation on WebUI https://github.com/bmorcelli/Launcher/issues/18
     * Fixed problem that prevented the WebUI from opening in some cases https://github.com/bmorcelli/Launcher/issues/16
     * Now M5Launcher formats the FAT VFS partition, so make sure you have saved your data to the SD card when using MicroPython, UIFlow, or MicroHydra https://github.com/bmorcelli/Launcher/issues/19
* 2.0.0:
     * SD: added folder creation, delete and rename files and folders, and copy and paste files
     * OTA (Over-The-Air update): Added feature to list the programs available in M5Burner and install them from the internet.
     * WebUI: Added a WebUI where you can manage your SD Card and install new binaries wirelessly
     * Some other minor features
     *
* 1.3.0:
     * Added support for MicroPython-based binaries (MicroHydra), with a 1MB FAT partition for Cardputer and StickCPlus2 and 64KB for StickC and Plus1.1
* 1.2.1:
     * Launcher now lowers the LCD power and fills the screen black before restart to prevent LCD burn when using apps that don't use the screen
     * Fixed display things and positions for the M5StickC
* 1.2.0:
     * Excluded ota_data.bin file as it is not needed
     * Excluded StartApp application
     * Excluded OTA_1 partitions from .csv files because I found out it is not needed
     * Reallocated free space into the "SPIFFS" partition, giving room for improvements and support for applications that use it (OrcaOne)
     * Added Bootscreen with battery monitor
     * Added Restart option and battery monitor to launcher
     * Added auto orientation to M5StickCs
     * Launcher does not create .bak files anymore!!
     * .bin file handling to avoid some errors: File is too big, file is not valid, etc etc..

* 1.1.3:
     * Fixed menu files that were occasionally hiding files and folders.
* 1.1.2:
     * Adjusted Magic numbers to work with some apps (Volos Watch).
* 1.1.1:
     * Changed OTA_0 partition size from 3MB to 6MB on Cardputer and M5StickCPlus2
     * ~~Added verification to identify MicroPython binaries and don't corrupt them with the cropping process (these apps still don't work, need more work...)~~
* 1.1.0:
     * Fixed issues that prevented M5Launcher from launching apps on Cardputer
* 1.0.1:
     * Fixed black screen and keyboard capture on Cardputer.
</details>


