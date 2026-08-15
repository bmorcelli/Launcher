# `custom_flags.py`

Turns a CYD board manifest (`source/*.json`) into a block of PlatformIO
`build_flags` that you paste into an environment.

## Why it exists

The CYD boards were described by one JSON manifest each. Those manifests carry
several hundred `-D` flags naming every pin of the panel, the touch controller
and the SD slot, in the vocabulary of the ESP-LCD component
(`ILI9341_SPI_BUS_MISO_IO_NUM`, `XPT2046_SPI_CONFIG_CS_GPIO_NUM`, ...). The
launcher's display layer does not speak that vocabulary — it wants `TFT_MISO`,
`TFT_DATABUS_N`, `CYD28_TouchR_CS` and friends — so this script used to run as a
PlatformIO `pre:` extra script and translate one into the other at build time.

That worked, but everything it produced was invisible: `platformio.ini` said
nothing about the panel, and IntelliSense only ever sees what the ini declares.
Working on a CYD meant reading this script to find out what the compiler had
actually been told, and the DisplayDrivers flags were buried inside it.

So the translation now happens once, offline. The environments live in
`<env>.ini` files next to this one and spell out every flag; the manifests in
`source/` are kept as the reference the flags were derived from, and are no
longer PlatformIO boards — the environments build against the generic `esp32`
and `esp32s3` boards in `boards/_jsonfiles/`.

## Usage

```sh
cd boards/CYD-2432S028
python3 custom_flags.py --source source/esp32-2432S024C.json
```

That prints the block. To append it to a file instead:

```sh
python3 custom_flags.py --source source/esp32-2432S024C.json \
                        --output CYD-2432S024C.ini
```

| option | meaning |
| --- | --- |
| `-s`, `--source` | board manifest to read (required) |
| `-o`, `--output` | append the block to this file; existing content is kept |

With `--output` the block is added at the **end** of the file, so the usual
order when porting a new model is:

1. write `<env>.ini` with the `[env:...]` header, `extends = CYD_Base` (or
   `CYDS3_Base`) and any `board_build.*` / `board_upload.*` the model needs;
2. run the script with `--output <env>.ini`;
3. change the `${env.build_flags}` line the script emits into
   `${CYD_Base.build_flags}` / `${CYDS3_Base.build_flags}`;
4. add the model's own flags (`DEVICE_NAME`, `OTA_TAG`, touch calibration,
   rotation, ...) underneath the generated block.

Step 4 goes last on purpose: PlatformIO feeds the compiler the flags in the
order they are listed, and a later `-D` wins over an earlier one. Keeping the
device block last reproduces the old precedence, where `build_flags` overrode
whatever the manifest and this script had set.

## What it emits

Groups, in order:

- **Chip / board identity** — `ARDUINO_ESP32*_DEV`, and whichever of
  `BOARD_HAS_PSRAM`, `ARDUINO_USB_MODE`, `ARDUINO_USB_CDC_ON_BOOT`,
  `-mfix-esp32-psram-cache-issue` and `-mfix-esp32-psram-cache-strategy=memw`
  the manifest carries. These configure the chip rather than the panel, so they
  have to survive the move out of the manifest.
- **Panel geometry and backlight** — `DISPLAY_WIDTH`, `DISPLAY_HEIGHT`,
  `GPIO_BCKL`, kept under their own names because `interface.cpp` still refers
  to them.
- **Input** — `HAS_TOUCH=1`.
- **Display** — the DisplayDrivers data bus (`TFT_DATABUS_N`) and panel
  controller (`TFT_DISPLAY_DRIVER_N`) chosen from the manifest's `DISPLAY_*`
  marker, plus the pins, size and offsets. See the DisplayDrivers README for the
  bus/driver number tables.
- **Touch** — XPT2046 becomes the launcher's `HAS_RESISTIVE_TOUCH` +
  `CYD28_TouchR_*`. GT911, CST816S and AXS15231B are driven by TouchLib and
  bb_captouch, which `interface.cpp` configures using the manifest's own macro
  names, so those keep their names and only gain a literal value.
- **SD card** — `SDCARD_*` from `TF_*`, or the CYD default wiring when the
  manifest has no `BOARD_HAS_TF`.

### Values are resolved, not referenced

The old script emitted `-DTFT_MISO=ILI9341_SPI_BUS_MISO_IO_NUM` and relied on
the manifest being the active board, which defined that symbol. It no longer is,
so every value is followed through the manifest until it is a literal:

```
-D TFT_MISO=12          # ILI9341_SPI_BUS_MISO_IO_NUM
```

The trailing comment records where the number came from. `GPIO_NUM_NC` resolves
to `-1`; `-U` lines in the manifest remove the symbol, the same as they would at
compile time. A value the manifest never defines is left as a symbol and
reported on stderr:

```
warning: TFT_PREF_SPEED=GFX_NOT_DEFINED is not defined by the manifest and was left as a symbol
```

That one is fine — `GFX_NOT_DEFINED` comes from Arduino_GFX. Any other warning
means the manifest is missing something.

## Adding a panel or a touch controller

`emit_display()` and `emit_touch()` are a chain of `if` branches keyed on the
manifest's `DISPLAY_*` / `TOUCH_*` marker. Add a branch, call `out.add(name,
value)` with the manifest's symbol as the value, and the resolver does the rest.
`out.add(name)` emits a bare `-D name`, and `out.raw(option)` passes a
non-`-D` compiler option straight through.
