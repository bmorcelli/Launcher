"""PlatformIO extra script: `pio run -t upload` pushes the build into a running
Launcher over serial, instead of flashing the whole chip.
Launcher stays intact; the pushed app boots after the Launcher startup delay.
Re-pushing replaces the same slot; if flash is full it fails rather than prompting.

In your project's platformio.ini:

    extra_scripts = post:/path/to/Launcher/tools/pio_launcher_upload.py
    upload_port = /dev/ttyACM0   ; optional, else PlatformIO auto-detects

Needs `pyserial` and `esptool` in PlatformIO's Python (pip install pyserial esptool).
"""

import os

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

flasher = os.path.join(os.path.dirname(os.path.abspath(__file__)), "serial_flasher.py")
app_name = os.path.basename(env.subst("$PROJECT_DIR"))  # noqa: F821


if not env.subst("$UPLOAD_PORT"):  # noqa: F821
    env.AutodetectUploadPort()  # noqa: F821

env.Replace(  # noqa: F821
    UPLOADCMD=f'"$PYTHONEXE" "{flasher}" -p "$UPLOAD_PORT" -n "{app_name}" -f $SOURCE'
)
