from pathlib import Path
from importlib import metadata
import os
import shutil
import subprocess
import sys
import sysconfig
import site


UNAVAILABLE_MESSAGE = "clang-format unavailable in this env, skipping"


def _scripts_dir_executable():
    name = "clang-format.exe" if os.name == "nt" else "clang-format"
    scripts_dirs = [sysconfig.get_path("scripts")]

    try:
        scripts_dirs.append(site.getusersitepackages().replace("site-packages", "Scripts"))
    except Exception:
        pass

    for scripts_dir in scripts_dirs:
        if not scripts_dir:
            continue

        executable = Path(scripts_dir) / name
        if executable.exists():
            return str(executable)

    return None


def _ensure_clang_format():
    try:
        metadata.distribution("clang-format")
    except metadata.PackageNotFoundError:
        try:
            subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "pip",
                    "install",
                    "clang-format",
                    "--disable-pip-version-check",
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except Exception:
            return None

    return shutil.which("clang-format") or _scripts_dir_executable()


def _project_root(platformio_env=None):
    if platformio_env:
        try:
            return Path(platformio_env.subst("$PROJECT_DIR")).resolve()
        except Exception:
            pass

    if "__file__" in globals():
        return Path(__file__).resolve().parents[1]

    return Path.cwd().resolve()


def _check_format(platformio_env=None):
    clang_format = _ensure_clang_format()

    if not clang_format:
        print(UNAVAILABLE_MESSAGE)
        return

    folders = [
        "src",
        "boards",
        "include",
        "lib/CYD-touch",
        "lib/utility"
    ]

    extensions = {".c", ".cpp", ".cc", ".h", ".hpp", ".hh"}
    project_root = _project_root(platformio_env)

    for folder in folders:
        p = project_root / folder

        if not p.exists():
            continue

        for file in p.rglob("*"):
            if file.suffix.lower() in extensions:
                try:
                    subprocess.run([clang_format, "-i", str(file)], check=True)
                except Exception:
                    print(UNAVAILABLE_MESSAGE)
                    return


def _check_clang_format(target, source, env):
    _check_format(env)
    env.Exit(0)


def _setup_platformio(platformio_env):
    platformio_env.AddCustomTarget(
        name="clang-format",
        dependencies=None,
        actions=[_check_clang_format],
        title="Apply Clang format",
        description="Apply Clang format"
    )


# Detects if running inside PlatformIO/SCons
try:
    from SCons.Script import Import

    Import("env")
    running_in_platformio = True
except Exception:
    env = None
    running_in_platformio = False

# runs the checker before every build
_check_format(env)

# Optional manual target for platformio: `pio run -t clang-format`
# Run clang-format without building the project
if running_in_platformio:
    _setup_platformio(env)
