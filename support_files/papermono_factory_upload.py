"""Route the PaperMono development upload to the factory application."""

from __future__ import annotations

import subprocess
from pathlib import Path

from SCons.Script import Import

Import("env")  # type: ignore


PAPERMONO_ENV = "m5stack-paper-mono"
FACTORY_APP_OFFSET = "0x10000"
OTADATA_OFFSET = "0xE000"
OTADATA_SIZE = "0x2000"


def _resolved(value, upload_env):
    return upload_env.subst(str(value)).strip('"')


def _is_hex_offset(value):
    text = str(value).strip().lower()
    return text.startswith("0x")


def _is_boot_app0(value, upload_env):
    return Path(_resolved(value, upload_env)).name.lower() == "boot_app0.bin"


def _remove_boot_app0(flags, upload_env):
    filtered = []
    removed = []
    index = 0
    while index < len(flags):
        if (
            index + 1 < len(flags)
            and _is_hex_offset(flags[index])
            and _is_boot_app0(flags[index + 1], upload_env)
        ):
            removed.append((flags[index], flags[index + 1]))
            index += 2
            continue
        filtered.append(flags[index])
        index += 1
    return filtered, removed


def _erase_otadata(target, source, env):
    """Erase otadata immediately before the final factory-safe upload."""

    flags = [_resolved(value, env) for value in env.get("UPLOADERFLAGS", [])]
    try:
        write_flash_index = flags.index("write-flash")
    except ValueError:
        env.Exit("PaperMono factory upload requires the esptool write-flash protocol")
        return

    uploader = _resolved(env.get("UPLOADER", ""), env)
    erase_command = [
        uploader,
        *flags[:write_flash_index],
        "erase-region",
        OTADATA_OFFSET,
        OTADATA_SIZE,
    ]
    print("[PaperMono] erasing otadata: 0xE000 length 0x2000")
    subprocess.run(erase_command, check=True)


if env.subst("${PIOENV}") == PAPERMONO_ENV:
    protocol = env.subst("$UPLOAD_PROTOCOL") or "esptool"
    if protocol != "esptool":
        env.Exit(f"PaperMono factory upload requires esptool, got: {protocol}")

    current_flags = list(env.get("UPLOADERFLAGS", []))
    filtered_flags, removed_images = _remove_boot_app0(current_flags, env)
    if removed_images:
        env.Replace(UPLOADERFLAGS=filtered_flags)
    else:
        env.Exit("PaperMono factory upload could not find boot_app0.bin to remove")

    # Use a literal application offset because pioarduino derives
    # ESP32_APP_OFFSET from ota_0 after board_upload.offset_address is read.
    filtered_extra_images = [
        image
        for image in env.get("FLASH_EXTRA_IMAGES", [])
        if not _is_boot_app0(image[1], env)
    ]
    env.Replace(
        FLASH_EXTRA_IMAGES=filtered_extra_images,
        ESP32_APP_OFFSET=FACTORY_APP_OFFSET,
        UPLOADCMD="$UPLOADER $UPLOADERFLAGS 0x10000 $SOURCE",
    )
    env.AddPreAction("upload", _erase_otadata)

    print("[PaperMono] factory-safe upload route configured:")
    print(f"  removed boot_app0 upload image(s): {removed_images}")
    print("  erase 0xE000 length 0x2000 before write")
    print("  write 0x0000 recovery bootloader")
    print("  write 0x8000 partitions.bin")
    print("  write 0x10000 firmware.bin")
    print("  payload range 0x1A0000-0xFFFFFF untouched")
