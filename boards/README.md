```
.
├── platformio.ini
├── boards
    ├── _JsonFiles
    │   └── [board].json
    ├── [board]
    |   └── conections.md
    │   ├── interface.cpp
    |   └── platformio.ini
    ├── pinouts
    │   ├── pins_arduino.h
    │   └── esp32[xx].h
    └── Readme.md

...
```

# Files
(Replace \[board] with the board name)

## boards/pinouts/\esp32[xx].h
Here is an official example and what we are actually using here, it must be common to all boards, only add files here if porting to a new esp32 variant:
https://github.com/espressif/arduino-esp32/blob/master/variants/esp32s3/pins_arduino.h

## boards/_JsonFiles/\[board].json
This is the board config. Look at other boards for whats needed.
Here is an offical example and what we are actually using here:
https://github.com/platformio/platform-espressif32/blob/master/boards/esp32-s3-devkitc-1.json


## boards/\[board]/connections.md
Document with pinouts for DIY devices.

## boards/\[board]/interface.cpp
This is where you do the board specific setup code

## boards/\[board]\platformio.ini
This is the platformio config for the device. Look at other boards for whats needed.
