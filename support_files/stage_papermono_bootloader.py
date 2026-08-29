"""Stage the selected PaperMono bootloader for the existing merge helper."""

import shutil
from pathlib import Path

from SCons.Script import Import

Import("env")

root = Path(env.subst("$PROJECT_DIR"))
source = root / "boards" / "_jsonfiles" / "papermono-recovery-bootloader.bin"
destination = Path(env.subst("$BUILD_DIR")) / "bootloader.bin"

if env.subst("${PIOENV}") == "m5stack-paper-mono":
    if not source.is_file():
        env.Exit(f"Missing PaperMono recovery bootloader: {source}")
    shutil.copyfile(source, destination)
    print(f"[PaperMono] staged custom bootloader: {source.name}")
