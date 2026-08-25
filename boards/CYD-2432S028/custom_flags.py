#!/usr/bin/env python3
"""Translate a CYD board manifest into ready-to-paste PlatformIO build_flags.

This used to run as a `pre:` extra_script, injecting the flags below straight
into the build. That kept them out of `platformio.ini` -- and therefore out of
IntelliSense, which only ever sees what the ini declares. Editing or debugging a
CYD meant reading this file to find out what the compiler had actually been
told.

It is now an offline generator: run it once per board, paste the result into the
environment, and every flag is visible both to the compiler and to the editor.

    python3 custom_flags.py --source source/esp32-2432S024C.json \
                            --output CYD-2432S024C.ini

Without --output the block goes to stdout; with it, the block is appended to the
end of the file (existing content is preserved).

See custom_flags.md for the full description.
"""

import argparse
import json
import os
import re
import sys

# Symbols the manifests reference but never define: they come from ESP-IDF
# headers. The generated flags must not depend on those headers being reachable
# from wherever the macro ends up being used, so resolve them here.
BUILTIN_SYMBOLS = {
    "GPIO_NUM_NC": "-1",
}

# Flags that are not part of the display/touch/SD description but still have to
# survive the move out of the board manifest: they configure the chip, not the
# panel. `ARDUINO_*_DEV` is the Arduino board identity macro.
PASSTHROUGH_DEFINES = (
    "BOARD_HAS_PSRAM",
    "ARDUINO_USB_MODE",
    "ARDUINO_USB_CDC_ON_BOOT",
)
PASSTHROUGH_OPTIONS = (
    "-mfix-esp32-psram-cache-issue",
    "-mfix-esp32-psram-cache-strategy=memw",
)
BOARD_IDENTITY_RE = re.compile(r"^ARDUINO_ESP32\w*_DEV$")

# Geometry and backlight: emitted as-is because interface.cpp and a few
# environments still spell them by name.
GEOMETRY_DEFINES = ("DISPLAY_WIDTH", "DISPLAY_HEIGHT", "GPIO_BCKL")


# ---------------------------------------------------------------------------
# manifest parsing
# ---------------------------------------------------------------------------


def unquote(raw):
    """Strip the quoting the manifests wrap each flag in ("'-D FOO=1'")."""
    text = str(raw).strip()
    while len(text) > 1 and text[0] == text[-1] and text[0] in "'\"":
        text = text[1:-1].strip()
    return text


def parse_extra_flags(extra_flags):
    """Return (symbols, options) from a manifest's build.extra_flags.

    `symbols` maps macro name -> value (None for a bare -D). `-U` removes an
    entry, matching what the compiler would do. `options` keeps everything that
    is not a -D/-U, in order.
    """
    symbols = {}
    options = []
    for raw in extra_flags:
        flag = unquote(raw)
        if not flag:
            continue
        match = re.match(r"^-([DU])\s*(.+)$", flag)
        if not match:
            options.append(flag)
            continue
        kind, body = match.group(1), match.group(2).strip()
        name, _, value = body.partition("=")
        name = name.strip()
        if kind == "U":
            symbols.pop(name, None)
        else:
            symbols[name] = value.strip() if value else None
    return symbols, options


def resolve(value, symbols, _seen=None):
    """Follow a value through the manifest until it is a literal.

    Only a value that is a bare identifier is followed -- an expression such as
    `(DISPLAY_WIDTH*DISPLAY_HEIGHT)` is left alone, since none of the flags this
    script emits use one.
    """
    if value is None:
        return None
    text = str(value).strip()
    if not re.fullmatch(r"[A-Za-z_]\w*", text):
        return text
    if text in BUILTIN_SYMBOLS:
        return BUILTIN_SYMBOLS[text]
    _seen = _seen or set()
    if text in _seen or text not in symbols or symbols[text] is None:
        return text
    _seen.add(text)
    return resolve(symbols[text], symbols, _seen)


class Emitter:
    """Collects the flags, resolving every value as it goes."""

    def __init__(self, symbols):
        self.symbols = symbols
        self.groups = []
        self.unresolved = []

    def group(self, title):
        self.groups.append((title, []))

    def add(self, name, value=None):
        """Add -D name[=value]; `value` may be a manifest symbol."""
        if value is None:
            self.groups[-1][1].append(("-D %s" % name, None))
            return
        raw = str(value)
        final = resolve(raw, self.symbols)
        note = raw if final != raw and raw != name else None
        if re.fullmatch(r"[A-Za-z_]\w*", final) and final not in ("true", "false"):
            self.unresolved.append((name, final))
        self.groups[-1][1].append(("-D %s=%s" % (name, final), note))

    def raw(self, option):
        self.groups[-1][1].append((option, None))

    def has_content(self):
        return any(entries for _, entries in self.groups)


# ---------------------------------------------------------------------------
# the board description itself
# ---------------------------------------------------------------------------


def emit_chip_flags(out, symbols, options):
    """Chip-level flags that used to ride along in the manifest."""
    out.group("Chip / board identity")
    for name in sorted(n for n in symbols if BOARD_IDENTITY_RE.match(n)):
        out.add(name, symbols[name])
    for name in PASSTHROUGH_DEFINES:
        if name in symbols:
            out.add(name, symbols[name])
    for option in PASSTHROUGH_OPTIONS:
        if option in options:
            out.raw(option)


def emit_geometry(out, symbols):
    out.group("Panel geometry and backlight")
    for name in GEOMETRY_DEFINES:
        if name in symbols:
            out.add(name, symbols[name])


def emit_display(out, symbols):
    """TFT_DATABUS_N / TFT_DISPLAY_DRIVER_N name the DisplayDrivers data bus and
    panel controller. See that library's README for the tables."""
    if "DISPLAY_ILI9341_SPI" in symbols:
        out.group("Display: ILI9341 over SPI")
        out.add("TFT_DATABUS_N", 0)  # Arduino_HWSPI
        out.add("TFT_DISPLAY_DRIVER_N", 4)  # Arduino_ILI9341
        out.add("TFT_MISO", "ILI9341_SPI_BUS_MISO_IO_NUM")
        out.add("TFT_MOSI", "ILI9341_SPI_BUS_MOSI_IO_NUM")
        out.add("TFT_SCLK", "ILI9341_SPI_BUS_SCLK_IO_NUM")
        out.add("TFT_CS", "ILI9341_SPI_CONFIG_CS_GPIO_NUM")
        out.add("TFT_DC", "ILI9341_SPI_CONFIG_DC_GPIO_NUM")
        out.add("TFT_RST", "ILI9341_DEV_CONFIG_RESET_GPIO_NUM")
        _emit_common_tft(out)

    elif "DISPLAY_ST7796_SPI" in symbols:
        out.group("Display: ST7796 over SPI")
        out.add("TFT_DATABUS_N", 0)  # Arduino_HWSPI
        out.add("TFT_DISPLAY_DRIVER_N", 2)  # Arduino_ST7796
        out.add("TFT_MISO", "ST7796_SPI_BUS_MISO_IO_NUM")
        out.add("TFT_MOSI", "ST7796_SPI_BUS_MOSI_IO_NUM")
        out.add("TFT_SCLK", "ST7796_SPI_BUS_SCLK_IO_NUM")
        out.add("TFT_CS", "ST7796_SPI_CONFIG_CS_GPIO_NUM")
        out.add("TFT_DC", "ST7796_SPI_CONFIG_DC_GPIO_NUM")
        out.add("TFT_RST", "ST7796_DEV_CONFIG_RESET_GPIO_NUM")
        _emit_common_tft(out)

    elif "DISPLAY_ST7789_SPI" in symbols:
        out.group("Display: ST7789 over SPI")
        out.add("TFT_DATABUS_N", 0)  # Arduino_HWSPI
        out.add("TFT_DISPLAY_DRIVER_N", 1)  # Arduino_ST7789
        out.add("TFT_MISO", "ST7789_SPI_BUS_MISO_IO_NUM")
        out.add("TFT_MOSI", "ST7789_SPI_BUS_MOSI_IO_NUM")
        out.add("TFT_SCLK", "ST7789_SPI_BUS_SCLK_IO_NUM")
        out.add("TFT_CS", "ST7789_SPI_CONFIG_CS_GPIO_NUM")
        out.add("TFT_DC", "ST7789_SPI_CONFIG_DC_GPIO_NUM")
        out.add("TFT_RST", "ST7789_DEV_CONFIG_RESET_GPIO_NUM")
        _emit_common_tft(out)

    elif "DISPLAY_AXS15231B_QSPI" in symbols:
        out.group("Display: AXS15231B over QSPI")
        out.add("TFT_DATABUS_N", 1)  # Arduino_ESP32QSPI
        out.add("TFT_DISPLAY_DRIVER_N", 22)  # Arduino_AXS15231B
        out.add("TFT_MISO", -1)
        out.add("TFT_MOSI", -1)
        out.add("TFT_D0", "AXS15231B_SPI_BUS_DATA0")
        out.add("TFT_D1", "AXS15231B_SPI_BUS_DATA1")
        out.add("TFT_D2", "AXS15231B_SPI_BUS_DATA2")
        out.add("TFT_D3", "AXS15231B_SPI_BUS_DATA3")
        out.add("TFT_SCLK", "AXS15231B_SPI_BUS_PCLK")
        out.add("TFT_CS", "AXS15231B_SPI_CONFIG_CS")
        out.add("TFT_DC", "AXS15231B_SPI_CONFIG_DC")
        out.add("TFT_RST", "AXS15231B_DEV_CONFIG_RESET")
        _emit_common_tft(out)

    elif "DISPLAY_ST7789_I80" in symbols:
        # D0-D7 are spread over both GPIO banks here, so it is the generic
        # 8-bit writer (bus 5) and not the faster same-bank PAR8Q (bus 2).
        out.group("Display: ST7789 over an 8-bit i80 bus")
        out.add("TFT_DATABUS_N", 5)  # Arduino_ESP32PAR8
        out.add("TFT_DISPLAY_DRIVER_N", 1)  # Arduino_ST7789
        out.add("TFT_INVERSION_OFF")
        out.add("TFT_WIDTH", "DISPLAY_WIDTH")
        out.add("TFT_HEIGHT", "DISPLAY_HEIGHT")
        out.add("TFT_CS", "ST7789_IO_I80_CONFIG_CS_GPIO_NUM")
        out.add("TFT_DC", "ST7789_I80_BUS_CONFIG_DC")
        out.add("TFT_RST", "ST7789_DEV_CONFIG_RESET_GPIO_NUM")
        out.add("TFT_WR", "ST7789_I80_BUS_CONFIG_WR")
        out.add("TFT_RD", "ST7789_RD_GPIO")
        for index in range(8):
            out.add("TFT_D%d" % index, "ST7789_I80_BUS_CONFIG_DATA_GPIO_D%d" % (index + 8))
        out.add("TFT_BCKL", "GPIO_BCKL")
        out.add("TFT_BL", "GPIO_BCKL")
        out.add("TFT_BUS_SHARED", 0)
        out.add("TFT_INVERTED", 0)
        _emit_offsets(out)

    elif "DISPLAY_ST7262_PAR" in symbols:
        out.group("Display: ST7262 RGB panel")
        _emit_rgb_panel(out, "ST7262", swap_red_and_blue=True, hsync_pol=0, vsync_pol=0)
        out.add("TFT_PREF_SPEED", 16000000)
        _emit_rgb_tail(out)

    elif "DISPLAY_ST7701_PAR" in symbols:
        out.group("Display: ST7701 RGB panel")
        _emit_rgb_panel(out, "ST7701", swap_red_and_blue=False, hsync_pol=1, vsync_pol=1)
        out.add("TFT_PREF_SPEED", "GFX_NOT_DEFINED")
        _emit_rgb_tail(out)

    else:
        out.group("Display: ILI9341 over SPI (CYD default wiring)")
        out.add("TFT_DATABUS_N", 0)  # Arduino_HWSPI
        out.add("TFT_DISPLAY_DRIVER_N", 4)  # Arduino_ILI9341
        out.add("TFT_MISO", 12)
        out.add("TFT_MOSI", 13)
        out.add("TFT_SCLK", 14)
        out.add("TFT_CS", 15)
        out.add("TFT_DC", 2)
        out.add("TFT_RST", -1)
        out.add("TFT_BL", 21)
        out.add("TFT_WIDTH", 240)
        out.add("TFT_HEIGHT", 320)
        _emit_offsets(out)


def _emit_common_tft(out):
    out.add("TFT_BL", "GPIO_BCKL")
    out.add("TFT_WIDTH", "DISPLAY_WIDTH")
    out.add("TFT_HEIGHT", "DISPLAY_HEIGHT")
    _emit_offsets(out)


def _emit_offsets(out):
    out.add("TFT_IPS", 0)
    out.add("TFT_COL_OFS1", 0)
    out.add("TFT_ROW_OFS1", 0)
    out.add("TFT_COL_OFS2", 0)
    out.add("TFT_ROW_OFS2", 0)
    out.add("ROTATION", 0)


def _emit_rgb_panel(out, prefix, swap_red_and_blue, hsync_pol, vsync_pol):
    out.add("TFT_DATABUS_N", 3)  # Arduino_ESP32RGBPanel
    out.add("TFT_DISPLAY_DRIVER_N", 49)  # Arduino_RGB_Display
    out.add("TFT_DE", "%s_PANEL_CONFIG_DE_GPIO_NUM" % prefix)
    out.add("TFT_VSYNC", "%s_PANEL_CONFIG_VSYNC_GPIO_NUM" % prefix)
    out.add("TFT_HSYNC", "%s_PANEL_CONFIG_HSYNC_GPIO_NUM" % prefix)
    out.add("TFT_PCLK", "%s_PANEL_CONFIG_PCLK_GPIO_NUM" % prefix)
    # Some of these panels wire R and B the other way round.
    red, blue = ("B", "R") if swap_red_and_blue else ("R", "B")
    for index in range(5):
        out.add("TFT_R%d" % index, "%s_PANEL_CONFIG_DATA_GPIO_%s%d" % (prefix, red, index))
    for index in range(6):
        out.add("TFT_G%d" % index, "%s_PANEL_CONFIG_DATA_GPIO_G%d" % (prefix, index))
    for index in range(5):
        out.add("TFT_B%d" % index, "%s_PANEL_CONFIG_DATA_GPIO_%s%d" % (prefix, blue, index))
    out.add("TFT_HSYNC_POL", hsync_pol)
    out.add("TFT_VSYNC_POL", vsync_pol)
    for axis in ("HSYNC", "VSYNC"):
        for part in ("FRONT_PORCH", "PULSE_WIDTH", "BACK_PORCH"):
            out.add(
                "TFT_%s_%s" % (axis, part),
                "%s_PANEL_CONFIG_TIMINGS_%s_%s" % (prefix, axis, part),
            )
    out.add("TFT_PCLK_ACTIVE_NEG", "%s_PANEL_CONFIG_TIMINGS_FLAGS_PCLK_ACTIVE_NEG" % prefix)


def _emit_rgb_tail(out):
    out.add("TFT_BL", "GPIO_BCKL")
    out.add("TFT_WIDTH", "DISPLAY_WIDTH")
    out.add("TFT_HEIGHT", "DISPLAY_HEIGHT")
    out.add("ROTATION", 0)


def emit_touch(out, symbols):
    """The touch controller.

    XPT2046 is read by the launcher's own CYD28_TouchscreenR, hence the
    CYD28_TouchR_* names. GT911/CST816S go through src/hal/inputs/touch.cpp
    (SensorLib) via TOUCH_CTRL_GT911/TOUCH_CTRL_CST8XX since Etapa 7 -- only
    their pin sub-defines keep the manifest's own macro names, which
    interface.cpp's touchCfg() reads directly. AXS15231B is still driven by
    bb_captouch, configured from the manifest's own macro names as before.
    """
    if "TOUCH_XPT2046_SPI" in symbols:
        out.group("Touch: XPT2046 (resistive, SPI)")
        out.add("HAS_RESISTIVE_TOUCH", 1)
        out.add("CYD28_TouchR_IRQ", "XPT2046_TOUCH_CONFIG_INT_GPIO_NUM")
        out.add("CYD28_TouchR_MISO", "XPT2046_SPI_BUS_MISO_IO_NUM")
        out.add("CYD28_TouchR_MOSI", "XPT2046_SPI_BUS_MOSI_IO_NUM")
        out.add("CYD28_TouchR_CLK", "XPT2046_SPI_BUS_SCLK_IO_NUM")
        out.add("CYD28_TouchR_CS", "XPT2046_SPI_CONFIG_CS_GPIO_NUM")

    elif "TOUCH_GT911_I2C" in symbols:
        out.group("Touch: GT911 (capacitive, I2C)")
        # TOUCH_CTRL_GT911 is the HAL selection macro (src/hal/inputs/touch.cpp)
        # -- not the same name as the manifest key above, since Etapa 7
        # migrated this family off TouchLib onto hal_touch_* / SensorLib.
        out.add("TOUCH_CTRL_GT911", 1)
        out.add("GT911_I2C_CONFIG_SDA_IO_NUM", "GT911_I2C_CONFIG_SDA_IO_NUM")
        out.add("GT911_I2C_CONFIG_SCL_IO_NUM", "GT911_I2C_CONFIG_SCL_IO_NUM")
        out.add("GT911_TOUCH_CONFIG_RST_GPIO_NUM", "GT911_TOUCH_CONFIG_RST_GPIO_NUM")
        out.add("GT911_TOUCH_CONFIG_INT_GPIO_NUM", "GT911_TOUCH_CONFIG_INT_GPIO_NUM")

    elif "TOUCH_CST816S_I2C" in symbols:
        out.group("Touch: CST816S (capacitive, I2C)")
        # TOUCH_CTRL_CST8XX is the HAL selection macro -- same Etapa 7 rename
        # as TOUCH_CTRL_GT911 above (this family also covers CST820/CST816S
        # via SensorLib's auto-detecting TouchDrvCSTXXX wrapper).
        out.add("TOUCH_CTRL_CST8XX", 1)
        out.add("CST816S_I2C_CONFIG_SDA_IO_NUM", "CST816S_I2C_CONFIG_SDA_IO_NUM")
        out.add("CST816S_I2C_CONFIG_SCL_IO_NUM", "CST816S_I2C_CONFIG_SCL_IO_NUM")
        out.add("CST816S_TOUCH_CONFIG_RST_GPIO_NUM", "CST816S_TOUCH_CONFIG_RST_GPIO_NUM")
        out.add("CST816S_TOUCH_CONFIG_INT_GPIO_NUM", "CST816S_TOUCH_CONFIG_INT_GPIO_NUM")

    if "AXS15231B_TOUCH_I2C_SDA" in symbols:
        out.group("Touch: AXS15231B (capacitive, I2C, in-panel)")
        for suffix in ("SDA", "SCL", "RST", "IRQ"):
            name = "AXS15231B_TOUCH_I2C_%s" % suffix
            out.add(name, name)


def emit_sdcard(out, symbols):
    out.group("SD card")
    if "BOARD_HAS_TF" in symbols:
        out.add("SDCARD_CS", "TF_CS")
        out.add("SDCARD_SCK", "TF_SPI_SCLK")
        out.add("SDCARD_MISO", "TF_SPI_MISO")
        out.add("SDCARD_MOSI", "TF_SPI_MOSI")
    else:
        out.add("SDCARD_CS", 5)
        out.add("SDCARD_SCK", 18)
        out.add("SDCARD_MISO", 19)
        out.add("SDCARD_MOSI", 23)


def build_flags_for(symbols, options):
    out = Emitter(symbols)
    emit_chip_flags(out, symbols, options)
    emit_geometry(out, symbols)
    out.group("Input")
    out.add("HAS_TOUCH", 1)
    emit_display(out, symbols)
    emit_touch(out, symbols)
    emit_sdcard(out, symbols)
    return out


# ---------------------------------------------------------------------------
# rendering
# ---------------------------------------------------------------------------


def render(out, source_label):
    lines = [
        "build_flags =",
        "\t${env.build_flags}",
        "",
        "\t# Generated by custom_flags.py from %s" % source_label,
        "\t# Re-run the script after touching the manifest; do not hand-edit.",
    ]
    width = 0
    for _, entries in out.groups:
        for flag, note in entries:
            if note:
                width = max(width, len(flag))
    for title, entries in out.groups:
        if not entries:
            continue
        lines.append("")
        lines.append("\t# %s" % title)
        for flag, note in entries:
            if note:
                lines.append("\t%s # %s" % (flag.ljust(width), note))
            else:
                lines.append("\t%s" % flag)
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Translate a CYD board manifest into PlatformIO build_flags.",
    )
    parser.add_argument(
        "-s",
        "--source",
        required=True,
        help="board manifest to read, e.g. source/esp32-2432S024C.json",
    )
    parser.add_argument(
        "-o",
        "--output",
        help="append the block to this file instead of printing it",
    )
    args = parser.parse_args(argv)

    try:
        with open(args.source, "r", encoding="utf-8") as handle:
            manifest = json.load(handle)
    except (OSError, ValueError) as error:
        parser.error("cannot read %s: %s" % (args.source, error))

    symbols, options = parse_extra_flags(manifest.get("build", {}).get("extra_flags", []))
    out = build_flags_for(symbols, options)
    block = render(out, os.path.basename(args.source))

    for name, value in out.unresolved:
        print(
            "warning: %s=%s is not defined by the manifest and was left as a symbol"
            % (name, value),
            file=sys.stderr,
        )

    if not args.output:
        sys.stdout.write(block)
        return 0

    existing = ""
    if os.path.exists(args.output):
        with open(args.output, "r", encoding="utf-8") as handle:
            existing = handle.read()
        if existing and not existing.endswith("\n"):
            existing += "\n"
        if existing.strip():
            existing += "\n"
    with open(args.output, "w", encoding="utf-8") as handle:
        handle.write(existing + block)
    print("wrote %d flags to %s" % (sum(len(e) for _, e in out.groups), args.output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
