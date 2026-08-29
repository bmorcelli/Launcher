"""Build the PaperMono recovery bootloader without rebuilding the Launcher app."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "boards" / "_jsonfiles" / "papermono-recovery-bootloader.bin"
DEFAULTS = ROOT / "support_files" / "papermono_bootloader_sdkconfig.defaults"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--packages", default=os.environ.get("PLATFORMIO_PACKAGES_DIR", ""))
    args = parser.parse_args()

    if not DEFAULTS.is_file():
        raise SystemExit(f"missing accepted sdkconfig defaults: {DEFAULTS}")
    with tempfile.TemporaryDirectory(prefix="papermono-recovery-") as temp_name:
        project = Path(temp_name)
        (project / "sdkconfig.defaults").write_text(
            "\n".join(
                [
                    DEFAULTS.read_text(encoding="utf-8"),
                    "CONFIG_APP_BUILD_BOOTLOADER=y",
                    "CONFIG_PARTITION_TABLE_CUSTOM=y",
                    f'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{(ROOT / "support_files" / "custom_16Mb.csv").resolve().as_posix()}"',
                    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
                    "CONFIG_ESPTOOLPY_FLASHMODE_QIO=y",
                    "CONFIG_ESPTOOLPY_FLASHFREQ_80M=y",
                ]
            ),
            encoding="utf-8",
        )
        env = os.environ.copy()
        packages = Path(args.packages).resolve() if args.packages else ROOT / ".pio" / "core" / "packages"
        idf = packages / "framework-espidf"
        cmake = packages / "tool-cmake" / "bin" / "cmake.exe"
        ninja = packages / "tool-ninja"
        idf_python = ROOT / ".pio" / "core" / "penv" / "Scripts" / "python.exe"
        idf_site_packages = ROOT / ".pio" / "core" / "penv" / ".espidf-5.5.4" / "Lib" / "site-packages"
        if not idf.is_dir() or not cmake.is_file() or not idf_python.is_file() or not idf_site_packages.is_dir():
            raise SystemExit("local ESP-IDF 5.5.4 build dependencies are incomplete")
        build = project / "build"
        configure = [
            str(cmake), "-S", str(idf / "components" / "bootloader" / "subproject"), "-B", str(build),
            "-G", "Ninja", f"-DIDF_PATH={idf}", "-DIDF_TARGET=esp32s3",
            f"-DSDKCONFIG={project / 'sdkconfig'}", f"-DSDKCONFIG_DEFAULTS={project / 'sdkconfig.defaults'}",
            f"-DPYTHON={idf_python}", "-DPYTHON_DEPS_CHECKED=1", "-DESP_IDF_VERSION=5.5.4",
        ]
        env["IDF_PATH"] = str(idf)
        env["IDF_TOOLS_PATH"] = str(packages)
        esptool_package = packages / "tool-esptoolpy"
        env["PYTHONPATH"] = os.pathsep.join(
            [str(esptool_package), str(idf_site_packages), env.get("PYTHONPATH", "")]
        )
        env["PATH"] = os.pathsep.join(
            [str(ninja), str(packages / "toolchain-xtensa-esp-elf" / "bin"), env.get("PATH", "")]
        )
        subprocess.run(configure, cwd=project, env=env, check=True)
        subprocess.run([str(cmake), "--build", str(build)], cwd=project, env=env, check=True)
        built = build / "bootloader.bin"
        if not built.is_file():
            raise SystemExit(f"bootloader build did not produce {built}")
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(built, OUTPUT)

    digest = hashlib.sha256(OUTPUT.read_bytes()).hexdigest()
    print(f"created {OUTPUT} size={OUTPUT.stat().st_size} sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
