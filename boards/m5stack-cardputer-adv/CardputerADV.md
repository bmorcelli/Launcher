# M5Stack Cardputer ADV Support

## Overview

This document details the implementation of M5Stack Cardputer ADV support for the Launcher firmware. The Cardputer ADV uses a different keyboard controller than the original Cardputer, requiring specific hardware adaptations.

## Hardware Differences

### Original Cardputer vs Cardputer ADV

| Component | Original Cardputer | Cardputer ADV |
|-----------|-------------------|---------------|
| Keyboard Controller | Direct GPIO matrix | TCA8418 I2C controller |
| I2C Address | N/A | 0x34 |
| SDA Pin | GPIO13 | GPIO8 |
| SCL Pin | GPIO15 | GPIO9 |
| Interrupt Pin | N/A | GPIO11 |

### TCA8418 Configuration

- **I2C Address**: 0x34
- **Matrix Size**: 7 rows × 8 columns
- **Communication**: I2C polling only
- **Key Detection**: 100ms polling interval

## Key Mapping

### Navigation Keys

| Function | Key | Matrix Position | Keycode | Notes |
|----------|-----|----------------|---------|-------|
| Previous | ↑ or ← | Row=3, Col=9 or Row=3, Col=6 | 0x39 or 0x36 | Either Up or Left arrow |
| Next | ↓ or → | Row=3, Col=10 or Row=4, Col=0 | 0x3A or 0x40 | Either Down or Right arrow |
| Enter/Select | Enter | Row=4, Col=3 | 0x43 | Primary selection |
| Escape/Back | Esc | Row=0, Col=1 | 0x01 | Back/Cancel |

### Cardputer ADV Navigation 

| Function | Key |
|----------|-------------------|
| **Previous** | `,` (←) or `;` (↑) |
| **Next** | `.` (→) or `/` (↓) |
| **Select** | Enter or GPIO0 |
| **Escape** | `` ` `` or Backspace |

### Additional I2C Devices Detected

- **0x18**: Likely accelerometer/IMU
- **0x34**: TCA8418 keyboard controller
- **0x69**: Likely additional sensor

## Implementation Details

### Files Modified

```
boards/m5stack-cardputer-adv/
├── platformio.ini          # ADV-specific build configuration
└── interface.cpp           # TCA8418 keyboard implementation
```

### Build Configuration

**platformio.ini** additions:
```ini
-DCARDPUTER_ADV=1
-DTCA8418_INT_PIN=11
-DTCA8418_I2C_ADDR=0x34
-DTCA8418_SDA_PIN=8
-DTCA8418_SCL_PIN=9
```

**Dependencies:**
```ini
adafruit/Adafruit TCA8418 @ ^1.0.1
```

### Key Implementation Functions

1. **_setup_gpio()**: Sets GPIO5 HIGH for SD card compatibility
2. **_post_setup_gpio()**: Initializes TCA8418 I2C communication

## Troubleshooting

### Original Issue
- **Symptom**: Device rebooted in endless loop after displaying "Using config.conf setup file"
- **Cause**: Original GPIO keyboard initialization incompatible with ADV hardware
- **Solution**: Conditional compilation to use TCA8418 for ADV variant

### SD Card Mount Issue
- **Symptom**: SD card would not mount due to hardware conflicts with additional I2C devices
- **Cause**: GPIO5 (SPI CS) interference from TCA8418 I2C controller and other sensors
- **Solution**: Set GPIO5 to HIGH during GPIO initialization to ensure SD card compatibility

### I2C Communication
- **Bus scan implemented**: Automatically detects available I2C devices
- **Multiple address fallback**: Tests common TCA8418 addresses
- **Pin configuration detection**: Tries multiple I2C pin combinations

## Build Instructions

### Prerequisites
- PlatformIO installed
- M5Stack Cardputer ADV hardware
- USB cable for programming

### Compilation
- Uncomment m5stack-cardputer-adv from platformio.ini
- Comment out any other boards
```bash
cd Launcher
pio run -e m5stack-cardputer-adv
```

### Upload
1. Put device in bootloader mode (hold GPIO0, press reset) may be needed in some cases
2. Upload firmware:
```bash
pio run -e m5stack-cardputer-adv -t upload --upload-port /dev/ttyACM0
```

### Serial Monitoring
```bash
pio device monitor --port /dev/ttyACM0 --baud 115200
```

## Debug Output

### Successful Initialization
```
DEBUG: Cardputer ADV - Initializing TCA8418 keyboard
DEBUG: Initializing I2C with SDA=8, SCL=9
DEBUG: Scanning I2C bus...
DEBUG: Found I2C device at address 0x18
DEBUG: Found I2C device at address 0x34
DEBUG: Found I2C device at address 0x69
DEBUG: Found 3 I2C devices
DEBUG: Attempting to initialize TCA8418 at address 0x34
DEBUG: TCA8418 found and initialized successfully!
```

### Key Press Events
```
DEBUG: Polling found key event (interrupt not working)
TCA8418 Key Event (polled): Row=3, Col=10, PRESSED (keycode=0x3A)
MAPPED: Down/Next pressed
```

## Performance Characteristics

- **Key response time**: ~100ms (polling interval)
- **Memory usage**: 25.3% RAM, 27.5% Flash
- **I2C communication**: Stable at standard speed (100kHz)
- **Power consumption**: Similar to original Cardputer

## Future Improvements

1. Jump-to-letter by pressing a letter on the keyboard

### Known Limitations
1. **Polling-only operation**: 100ms polling interval (interrupt pin not connected in hardware)
2. **Limited key mapping**: Only navigation keys currently mapped
3. **No key combinations**: Modifier keys not implemented

## Validation Tests

### Functional Tests Passed
- ✅ Device boots without reboot loop
- ✅ TCA8418 detection and initialization
- ✅ I2C communication stable
- ✅ Key press detection working
- ✅ Navigation key mapping functional
- ✅ Launcher menu navigation operational
- ✅ SD card mounting and file system access
- ✅ Power saving prevention during keyboard navigation
- ✅ All launcher features accessible

### Hardware Compatibility
- ✅ M5Stack Cardputer ADV hardware
- ✅ ESP32-S3 processor support
- ✅ 8MB flash memory utilization
- ✅ I2C bus sharing with other sensors

## Support Information

### Debugging Commands
```bash
# Build only
pio run -e m5stack-cardputer-adv

# Upload with bootloader mode
pio run -e m5stack-cardputer-adv -t upload --upload-port /dev/ttyACM0

# Monitor output
pio device monitor --port /dev/ttyACM0 --baud 115200

# Clean build
pio run -e m5stack-cardputer-adv -t clean
```

### Configuration Files
- **Main config**: `platformio.ini` (default environment disabled)
- **ADV config**: `boards/m5stack-cardputer-adv/platformio.ini`
- **Hardware interface**: `boards/m5stack-cardputer-adv/interface.cpp`

---

**Author**: n0xa
**Date**: September 9, 2025  
**Status**: Complete and Functional  
**Tested**: M5Stack Cardputer ADV and M5Stack Cardputer
