from pathlib import Path
from importlib import metadata
import os
import shutil
import subprocess
import sys
import sysconfig
import site


UNAVAILABLE_MESSAGE = "clang-format unavailable in this env, skipping"
FOLDERS = [
    "src",
    "boards",
    "include",
    "lib/CYD-touch",
    "lib/utility",
    "lib/xteink_panel",
]
EXTENSIONS = {".c", ".cpp", ".cc", ".h", ".hpp", ".hh"}


def _option_enabled(env_name, args):
    value = os.environ.get(env_name, "")

    if value.lower() in {"1", "true", "yes", "on"}:
        return True

    return any(arg in sys.argv for arg in args)


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


def _git_files(project_root, args):
    try:
        result = subprocess.run(
            ["git", "-C", str(project_root), *args],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except Exception:
        return []

    return [line for line in result.stdout.splitlines() if line]


def _modified_files(project_root):
    pathspec = ["--", *FOLDERS]
    changed_files = _git_files(
        project_root,
        ["diff", "--name-only", "--diff-filter=ACMR", "HEAD", *pathspec],
    )
    untracked_files = _git_files(
        project_root,
        ["ls-files", "--others", "--exclude-standard", *pathspec],
    )

    files = []
    seen = set()

    for relative_path in [*changed_files, *untracked_files]:
        if relative_path in seen:
            continue

        seen.add(relative_path)
        file = project_root / relative_path

        if file.is_file() and file.suffix.lower() in EXTENSIONS:
            files.append(file)

    return files


def _all_files(project_root):
    files = []

    for folder in FOLDERS:
        p = project_root / folder

        if not p.exists():
            continue

        for file in p.rglob("*"):
            if file.is_file() and file.suffix.lower() in EXTENSIONS:
                files.append(file)

    return sorted(files)


def _print_files(project_root, files):
    if not files:
        print("clang-format files: none")
        return

    print("clang-format files:")

    for file in files:
        print(f"  {file.relative_to(project_root).as_posix()}")


def _check_format(platformio_env=None):
    project_root = _project_root(platformio_env)
    force_all = _option_enabled("CLANG_FORMAT_ALL", ["--all", "--clang-format-all"])
    check_only = _option_enabled("CLANG_FORMAT_CHECK", ["--check", "--clang-format-check"])
    files = _all_files(project_root) if force_all else _modified_files(project_root)

    _print_files(project_root, files)

    if not files:
        return True

    clang_format = _ensure_clang_format()

    if not clang_format:
        print(UNAVAILABLE_MESSAGE)
        return True

    for file in files:
        try:
            if check_only:
                subprocess.run([clang_format, "--dry-run", "--Werror", str(file)], check=True)
            else:
                subprocess.run([clang_format, "-i", str(file)], check=True)
        except Exception:
            if check_only:
                print("clang-format check failed. Run clang-format before submitting.")
                return False

            print(UNAVAILABLE_MESSAGE)
            return True

    return True


def _check_clang_format(target, source, env):
    env.Exit(0 if _check_format(env) else 1)


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
format_check_passed = _check_format(env)

if not format_check_passed:
    if running_in_platformio:
        env.Exit(1)
    else:
        sys.exit(1)

# Optional manual target for platformio: `pio run -t clang-format`
# Run clang-format without building the project
if running_in_platformio:
    _setup_platformio(env)
