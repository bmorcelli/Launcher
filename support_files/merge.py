from pathlib import Path
from SCons.Script import Import

# Import PlatformIO's SCons environment
Import("env")
senv = env  # avoid shadowing with the action's 'env' parameter

# Optional: keep your extra flag
senv.Append(CXXFLAGS=["-Wno-conversion-null"])

# ---- Detect MCU and map bootloader offsets ----
board_config = senv.BoardConfig()
mcu = (board_config.get("build.mcu") or senv.get("BOARD_MCU") or "").lower()

BOOT_OFFSETS = {
    "esp32": 0x1000,   # ESP32
    "esp32s3": 0x0000,   # ESP32-S3
    "esp32c5": 0x2000,   # ESP32-C5
    "esp32p4": 0x2000,   # ESP32-P4
}
boot_offset = BOOT_OFFSETS.get(mcu, 0x0000)   # safe fallback

# Default offsets (adjust if your partitions.csv uses custom addresses)
PART_TABLE_OFFSET = 0x8000
NVS_OFFSET = 0x9000
APP_OFFSET = 0x10000

# Paths
build_dir = Path(senv.subst("$BUILD_DIR"))
proj_dir = Path(senv.subst("$PROJECT_DIR"))
pioenv = senv.subst("${PIOENV}")

boot_bin = build_dir / "bootloader.bin"
part_bin = build_dir / "partitions.bin"
nvs_bin = proj_dir / "support_files/UiFlow2_nvs.bin"
app_bin  = build_dir / "firmware.bin"

out_bin = proj_dir / f"Launcher-{pioenv}.bin"

# Esptool from PlatformIO + Python executable
esptool_pkg = senv.PioPlatform().get_package_dir("tool-esptoolpy")
esptool_py = str(Path(esptool_pkg) / "esptool")
python_exe = senv.get("PYTHONEXE", "python")

chip_arg = mcu if mcu else "esp32"


def _merge_bins_callback(target, source, env):
    """
    Post-action callback executed after firmware.bin is built.
    Merges bootloader, partitions, and app into a single binary.
    """
    # Check files
    missing = [p for p in [boot_bin, part_bin, app_bin] if not p.exists()]
    if missing:
        print("[merge_bin] Missing files, merge aborted:")
        for p in missing:
            print(f" - {p}")
        return

    # Quote paths for Windows safety
    def q(p): return f'"{p}"'

    # Initialize cmd_parts as a list
    cmd_parts = [
        "pio pkg exec -p \"tool-esptoolpy\" -- esptool.py",
        "--chip", chip_arg,
        "merge_bin",
        "--output", q(out_bin),
        hex(boot_offset), q(boot_bin),
        hex(PART_TABLE_OFFSET), q(part_bin),
    ]

    flag_path = Path(env.subst("$PROJECT_DIR")) / ".pio" / "build" / env.subst("${PIOENV}") / "nvs_flag.txt"
    nvs_flag_present = flag_path.exists()
    if nvs_flag_present:
        print("[merge_bin] NVS flag file detected. Including UiFlow2_nvs.bin in the merge...")
        cmd_parts.extend([
            hex(NVS_OFFSET), q(nvs_bin),
        ])

    cmd_parts.extend([
        hex(APP_OFFSET), q(app_bin),
    ])

    # Join the list into a single string for execution
    cmd = " ".join(cmd_parts)

    print("[merge_bin] Merging binaries:")
    print(" ", cmd)
    rc = env.Execute(cmd)
    if rc != 0:
        print(f"[merge_bin] Failed with exit code {rc}")
    else:
        try:
            size = out_bin.stat().st_size
        except FileNotFoundError:
            size = 0
        print(f"[merge_bin] Success -> {out_bin} ({size} bytes)")


# Automatically run after firmware.bin is generated
senv.AddPostAction(str(app_bin), _merge_bins_callback)

# Optional manual target: depend on the three binaries and run the same callback
senv.AddCustomTarget(
    name="build-firmware",
    dependencies=[str(boot_bin), str(part_bin), str(app_bin)],
    actions=[_merge_bins_callback],
    title="Build Firmware (merge)",
    description="Merge bootloader + partitions + app into a single .bin file"
)

# Keep your upload-nobuild helper
senv.AddCustomTarget(
    name="upload-nobuild",
    dependencies=None,
    actions=[f"platformio run -t upload -t nobuild -e {pioenv}"],
    title="Upload Nobuild",
    description="Run upload without rebuilding"
)
