# Launcher
Application Launcher for M5Stack, Lilygo, CYDs, Marauder and ESP32 devices.


<p align="center" width="100%">
    <img src="https://github.com/bmorcelli/Launcher/blob/main/M5Launcher.png?raw=true"> <img src="https://github.com/bmorcelli/Launcher/blob/main/New Launcher.jpg?raw=true" width="240" height="135"> <img src="https://github.com/bmorcelli/Launcher/blob/main/Launcher_anim.webp?raw=true" width="auto" height="135">
</p>

Join our [Discord community](https://discord.gg/BE9by2a2FF)

## How to Install
* Use the Flasher: [Launcher Flasher](https://bmorcelli.github.io/Launcher/).
* Use M5Burner, or
* Download the .bin file from Releases for your device and use https://web.esphome.io/ or esptool.py and flash the file: `Launcher-{ver}-{YourDevice}.bin` into your device.

## How to Use
* Turn your Device on
* Press M5 (Enter) in the Launcher Start Screen to start Launcher
* Choose OTA to install new binaries from M5Burner repo
* After installed, when turn on the device, if you don't press anything, the installed program will be launched.

## With Launcher you'll be able to:
<details>
  <summary><h3>OTA - OTA Update</h3></summary>

- Install binaries from M5Burner repository (yes, online, without the need of a USB Cable)
- Install binaries from a WebUI, that you can start from CFG option, installing binaries you have on your computer or smartphone
- Install binaries from your SD Card
</details>
<details>
  <summary><h3>SD - SDCard Management</h3></summary>
- Create new Folders,
- Delete files and folders,
- Rename files,
- Copy and paste files,
- Install binaries

</details>
<details>
  <summary><h3>WUI - Web User Interface</h3></summary>
- Manage files on the SD Card
- Install Binaries wirelessly using OTA Update option
- Deploy instalation through the file list

</details>
<details>
  <summary><h3>CFG - Configurations (Customization)</h3></summary>
- Charge Mode
- Change brightness
- Change Dim Time
- Change UI Color
- Avoid/Ask Spiffs (Change to not ask to install Spiffs file system, only Orca One uses this feature)
- Change rotation
- All files/Only Bins (see all files or only .bins - default)
- Change Partition Scheme (allows installing big apps or UiFlow2, for example)
- List of Partitions
- Clear FAT partition
- Save SPIFFS (Save a copy of the SPIFFS partition to restore when needed)
- Save FAT vfs (Save a copy of the FAT partition to restore when needed)
- Restore SPIFFS
- Restore FAT vfs
</details>

<details>
  <summary><h3>Tips</h3></summary>
* Having an SD card is good for better experience, but not really needed. [SDCard Hat for M5StickCs](https://www.thingiverse.com/thing:6459069)
* You can learn more about how it works o [Launcher Wiki](https://github.com/bmorcelli/Launcher/wiki/Explaining-the-project).
* Where/How do I find Binaries to launch -> [Obtaining binaries to launch](https://github.com/bmorcelli/Launcher/wiki/Obtaining-binaries-to-launch)
* Now you can download binaries from [HERE!](https://bmorcelli.github.io/Launcher/m5lurner.html)
</details>

## Known Issues
* UiFlow 1 doesn´t work with Launcher.. it uses an old MicroPython distro, that uses an old ESP-IDF distro with lots os secrets that I couldn´t figure out.

## To-Do list
Things that needs to be done in next updates
     * [ ] LVGL for e-paper displays
     * [ ] Move to ESP-IDF Platform


## Latest Changelog

* 2.6.0:
     * [x] Pulling data from my new api, integrating all devices list with m5burner api and counting downloads into the m5burner database.
     * [x] OTA Pagination (pages of 100 firmware ordered by download-default)
     * [x] New Device: [M5Stack Tab5](https://shop.m5stack.com/products/m5stack-tab5-iot-development-kit-esp32-p4?ref=Pirata).
     * [x] New Device: [Lilygo T-HMI](https://www.lilygo.cc/products/t-hmi?bg_ref=sDI8Bh4HmO)
     * [x] New Feature: Backup SPIFFS/FAT now prompts to merge data into a chosen binary, so you can install firmware and data choosign `SPIFFS Yes` on install and backup B Binaries now have incremental names.
     * [x] New Feature: Added Filter and ordering to the fimware list.
     * [x] New Feature: Added *Starred* firmware list (controlled by me.. support the project to have yout firmware into the starred list.).
     * [x] New Feature: Added "Favorite" (Need SD Card), where you an add the firmwares from the OTA list, or Manually add your binary links from your local server or whatever.
     * [x] config.conf changes:
```
...
    "favorite": [
      { // Example of firmware added into Favorites through OTA function
        "name": "Evil-Cardputer-7h30th3r0n3",
        "fid": "2128851a0c98a4c1d15ac1a327b49812",
        "link": ""
      },
      { // Example of my custom link file added manually editiing this file
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


<details>
  <summary><h2>Older Changelogs</h2></summary>

* 2.5.3:
     * [x] Restored T-Deck OTA
     * [x] Refined T-Deck Touchscreen inputs

* 2.5.2:
     * [x] Fixed Marauder V6 touchscreen and CYDs touchscreen unresponsive [issue](https://github.com/bmorcelli/Launcher/issues/210) and fixed Dim screen
     * [x] Fixed Marauder Mini and V7 screen dimming.
     * [x] Fixed Smoochiee board inputs and SDCard [issue](https://github.com/bmorcelli/Launcher/issues/209)
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
          * [x] OTA lists and install
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
     * [x] Fixed random restartings when dimming screen
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
     * [x] StickCs Power Btn and Prev butn now act to go upwards on Menus... long press to exit menu
* 2.2.5:
     * [x] Changed framework to remove all Watchdog Timers https://github.com/bmorcelli/Launcher/issues/61 https://github.com/bmorcelli/Launcher/issues/63 and solve some SD related issues
     * [x] Set Grove pins to LOW state on StickCs and Cardputer to avoid 433Mhz jamming while RF433T is connected
* 2.2.4:
     * [x] Finally ported to CoreS3 and CoreS3-SE
     * [x] Added reset to watchdog on WebUI and check for free memory when loading files to WebUI, to avoid crashes.
* 2.2.3:
     * [x] StickC, Plus and Plus2: removed power btn from skip logic at start.
     * [x] Changed EEPROM Addresses to avoid problems with the new Bruce and other firmwares..
* 2.2.2:
     * [x] Port for [CYD-2432S028R](https://www.amazon.com/dp/B0BVFXR313) and [CYD-2-Usb](https://www.amazon.com/dp/B0CLR7MQ91)
     * [x] Added check after finish the download that will delete failed downloaded files
* 2.2.1:
     * [x] Port for [Lilygo T-Display S3 Touch](https://www.lilygo.cc/products/t-display-s3?variant=42351558590645)
     * [x] Fixed JSON read/write
* 2.2.0:
     * [x] M5Launcher 2.2+ now can be updated OverTheAir or Using SD Card
     * [x] Ui Color settings (can be customized on /config.conf file)
     * [x] Reduced flickering on SD files navigation
     * [x] Fixed problem when firmware names have "/" that prevent downloading
     * [x] Appended firmware Version into download Name.
     * [x] Added Dim time to lower brightness and CPU freq while idle
     * [x] Added "Chage Mode" on settings, reducing CPU fre to 80Mhz and brightness to 5%, https://github.com/bmorcelli/Launcher/issues/40
     * [x] Fixed Download progressbar https://github.com/bmorcelli/Launcher/issues/41
     * [x] Change default folder for download (manually on /config.conf) https://github.com/bmorcelli/Launcher/issues/15
     * [x] Save more SSIDs and pwd, connect automatically if is a known network (config.conf) https://github.com/bmorcelli/Launcher/issues/30
     * [x] Slightlty increased Wifi Download/OTA Speed, using a customized framework.
* 2.1.2:
     * [x] Fixed OTA error message
     * [x] Increased Options Menu width and reduced menu flickering
* 2.1.1:
     * [x] Fixed UIFlow Compatibility
     * [x] Fixed SD card issues
     * [x] Small Fixes https://github.com/bmorcelli/Launcher/issues/37
* 2.1.0:
     * [x] Core Fire (all 16Mb Core devices) and Core2 compatibility
     * [x] Turn SPIFFs update optional (turne off by default) (config.conf)
     * [x] De-Sprite-fied the screens for Core devices (No PSRam, unable to handle huge Sprites) https://github.com/bmorcelli/Launcher/issues/34
     * [x] Make keyboard work with touchscreen capture in Core devices
     * [x] Dedicated btn for WebUI on main screen https://github.com/bmorcelli/Launcher/issues/22
     * [x] Multiple files upload on WebUI https://github.com/bmorcelli/Launcher/issues/28
     * [x] Update FAT vfs partition to make compatible with UIFlow2 https://github.com/bmorcelli/Launcher/issues/29
     * [x] Partition changer, to allow running DOOM and UIFlow on Cardputer and StickC
     * [x] Fixed (increased number of files) https://github.com/bmorcelli/Launcher/issues/33
     * [x] Backup and Restore FAT and SPIFFS Filesystems. If you use UIFlow, you can save all sketches into your device and make a backup to restore after reinstall UIFlow or MicroHydra or CircuitPython
* 2.0.1:
     * Fixed UIFlow Instalation https://github.com/bmorcelli/Launcher/issues/20
     * Fixed Folder creation on WebUI https://github.com/bmorcelli/Launcher/issues/18
     * Fexed problem that preven webUI to open in some cases https://github.com/bmorcelli/Launcher/issues/16
     * Now M5Launcher formats FAT vfs partition, so make sure you have saved your data into SDCard when using MicroPython, UIFlow or MicroHydra https://github.com/bmorcelli/Launcher/issues/19
* 2.0.0:
     * SD: added Folder creation, delete an rename files and folders, copy and paste files
     * OTA (Over-The-Air update): Added feature to list the programs available in M5Burner and install it fro the internet.
     * WebUI: Added a WebUI where you can manage your SD Card and install new binaries wirelessly
     * Some other minnor features
     *
* 1.3.0:
     * Added support to Micropython based binaries (MicroHydra), with 1Mb FAT partition to Cardputer and StickCPlus2 and 64kb to StickC and Plus1.1
* 1.2.1:
     * Launcher now lower the LCD power and fill the screen black before restart, to prevent lcd burn when using apps that don't use the Screen
     * Fixed display things and positions for the M5StickC
* 1.2.0:
     * Excluded ota_data.bin file as it is not needed
     * Excluded StartApp application
     * Excluded OTA_1 partitions form .csv files because i found out it is not needed
     * Realocated free spaces into "SPIFFS" partition, giving room to improvements, and support to applications that use it (OrcaOne)
     * Added Bootscreen with battery monitor
     * Added Restart option and battery monitor to launcher
     * Added auto orientation to M5StickCs
     * Laucher does not create .bak files anymore!!
     * .bin file handling to avoid some errors: File is too big, file is not valid, etc etc..

* 1.1.3:
     * Fixed menu files that ware occasionally hiding files and folders.
* 1.1.2:
     * Adjusted Magic numbers to work with some apps (Volos Watch).
* 1.1.1:
     * Changed OTA_0 Partition size from 3Mb to 6Mb on Cardputer and M5StickCPlus2
     * ~~Added verification to identify MicroPython binaries and don't corrupt them with the cropping process (these apps still don't work, need more work...)~~
* 1.1.0:
     * Fixed issues that prevented M5Launcher to launch apps on Cardputer
* 1.0.1:
     * Fixed blackscreen and keyboard capture on Cardputer.
</details>

