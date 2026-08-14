# Launcher
Application Launcher for
- M5Stack devices: Cardputer, M5StickC, M5StickC Plus, M5StickC Plus 2, Core (16Mb), Core2 & Tough and CoreS3;
- Lyligo devices: T-Display S3 Touch, T-Deck, T-Embed, T-Embed CC1101;
- CYD devices: CYD-2432S028 and 2USB variant;
- Espressif ESP32 (4Mb and 8Mb), ESP32-S3 (4Mb, 8Mb and 16Mb);


<p align="center" width="100%">
    <img src="https://github.com/bmorcelli/M5Stick-Launcher/blob/WebPage/M5Launcher.png?raw=true">  <img src="https://github.com/bmorcelli/M5Stick-Launcher/blob/WebPage/New Launcher.jpg?raw=true" width="240" height="135"> 
</p>

## With Launcher you'll be able to:
### *OTA* OTA Update
- Install binaries from M5Burner repository (yes, online, without the need of a USB Cable)
- Install binaries from a WebUI, that you can start from CFG option, installing binaries you have on your computer or smartphone
- Install binaries from your SD Card

### *SD* SD Card Management
- Create new Folders,
- Delete files and folders,
- Rename files,
- Copy and paste files,
- Install binaries

### *WUI* Web User Interface 
- Install applications using the OTA Update
- Manage SD Card (if available)

### *CFG* Configurations (Customization)
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

## tips
* Having an SD card is good for better experience, but not really needed. [SDCard Hat for M5StickCs](https://www.thingiverse.com/thing:6459069) 
* You can learn more about how it works o [Launcher Wiki](https://github.com/bmorcelli/M5Stick-Launcher/wiki/Explaining-the-project).
* Where/How do I find Binaries to launch -> [Obtaining binaries to launch](https://github.com/bmorcelli/M5Stick-Launcher/wiki/Obtaining-binaries-to-launch)
* Now you can download binaries from [HERE!](https://bmorcelli.github.io/M5Stick-Launcher/catalog.html)

## How to Install
* Use the Flasher: [Launcher Flasher](https://bmorcelli.github.io/M5Stick-Launcher/index.html).
* Use M5Burner, or
* Download the .bin file from Releases for your device and use https://web.esphome.io/ or esptool.py and flash the file: `Launcher-{ver}-{YourDevice}.bin` into your device.

## How to Use
* Turn your Device on
* Press M5 (Enter) in the Launcher Start Screen to start Launcher
* Choose OTA to install new binaries from M5Burner repo
* After installed, when turn on the device, if you don't press anything, the installed program will be launched.

## Install from source
Sourcecode will be released in the future..

## Known Issues
* UiFlow 1 doesn't work with Launcher.. it uses an old MicroPython distro, that uses an old ESP-IDF distro with lots os secrets that I couldn't figure out.

