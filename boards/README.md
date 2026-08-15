```
.
├── platformio.ini
├── boards
    ├── _jsonfiles
    │   └── [board].json
    ├── [board]
    |   └── conections.md
    │   ├── interface.cpp
    |   └── platformio.ini
    └── Readme.md

...
```

# Files
(Replace \[board] with the board name)

## boards/_JsonFiles/\[board].json
This is the board config. Look at other boards for whats needed.
Here is an offical example and what we are actually using here:
https://github.com/platformio/platform-espressif32/blob/master/boards/esp32-s3-devkitc-1.json
DO NOT TOUCH THESE FILES, Add only if the variant one doesn't exists.

## boards/\[board]/connections.md
Document with pinouts for DIY devices.

## boards/\[board]/interface.cpp
This is where you do the board specific setup code.

## boards/\[board]\platformio.ini
This is the platformio config for the device. Look at other boards for whats needed.
