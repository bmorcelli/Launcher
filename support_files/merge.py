from pathlib import Path
import csv
import subprocess
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

if mcu=="esp32p4":
    APP_OFFSET = 0x20000
    nvs_bin = proj_dir / "support_files/UiFlow2_nvs_p4.bin"

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

    # ---- Read partition CSV to get test partition size and ota_0 offset ----
    part_csv_name = board_config.get("build.partitions") or env.GetProjectOption(
        "board_build.partitions", default=""
    )
    part_csv = proj_dir / part_csv_name if part_csv_name else proj_dir / "partitions.csv"
    test_size = None
    ota0_offset = None
    if part_csv.exists():
        with open(part_csv, newline="") as f:
            reader = csv.reader(f)
            for row in reader:
                if not row or row[0].startswith("#"):
                    continue
                cols = [c.strip() for c in row]
                if len(cols) < 5:
                    continue
                name, ptype, subtype, offset, size = cols[:5]
                subtype = subtype.lower()
                if subtype == "test" and test_size is None:
                    try:
                        test_size = int(size, 0)
                    except ValueError:
                        pass
                if subtype == "ota_0" and ota0_offset is None:
                    try:
                        ota0_offset = int(offset, 0)
                    except ValueError:
                        pass

    # ---- Firmware size check against test partition ----
    if test_size:
        fw_size = app_bin.stat().st_size
        percent = (fw_size / test_size) * 100 if test_size else 0
        bar_len = 20
        filled = int(bar_len * fw_size / test_size)
        bar = "=" * filled + " " * (bar_len - filled)
        print(
            f"Launcher: [{bar}] {percent:.1f}% (used 0x{fw_size:X} bytes of 0x{test_size:X} of test partition)"
        )
        if fw_size > test_size:
            print("[merge_bin] Error: firmware.bin exceeds test partition size")
            env.Exit(1)


    # Quote paths for Windows safety
    def q(p): return f'"{p}"'

    # Initialize cmd_parts as a list
    cmd_parts = [
        "pio pkg exec -p \"tool-esptoolpy\" -- esptool",
        "--chip", chip_arg,
        "merge-bin",
        "--output", q(out_bin),
    ]

    if mcu=="esp32p4":
        cmd_parts.extend([
            hex(0x0),"./support_files/esp32p4.bin",
            hex(PART_TABLE_OFFSET), q(part_bin),
        ])
    else:
        cmd_parts.extend([
            hex(boot_offset), q(boot_bin),
            hex(PART_TABLE_OFFSET), q(part_bin),
        ])




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

    print("[merge_bin] Merging binaries...")
    result = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    rc = result.returncode
    # rc = env.Execute(cmd) # debug purposes

    if rc != 0:
        print(f"[merge_bin] Failed with exit code {rc}")
        # env.Exit(rc)
    else:
        try:
            size = out_bin.stat().st_size
        except FileNotFoundError:
            size = 0
        print(f"[merge_bin] Success -> {out_bin} ({size} bytes)")
        if ota0_offset:
            if size < ota0_offset:
                print("[Final bin] Valid bin to upload")
            else:
                print(
                    f"[Final bin] Error: bin size 0x{size:X} exceeds ota_0 offset 0x{ota0_offset:X}"
                )
                env.Exit(1)


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
