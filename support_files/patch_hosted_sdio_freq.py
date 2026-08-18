import os

Import("env")  # type: ignore

# Arduino's esp32-hal-hosted.c always builds its SDIO config from
# INIT_DEFAULT_HOST_SDIO_CONFIG(), which pulls clock_freq_khz from the prebuilt
# esp32-arduino-libs' baked-in Kconfig default (CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ,
# 40 MHz) - there is no public API to override just the frequency at runtime (only
# pins, via hostedSetPins()). This patches the one line that builds that struct so a
# board can override the frequency via -D LAUNCHER_HOSTED_SDIO_FREQ_KHZ=<value> in its
# own platformio.ini; boards that don't define it are unaffected. Idempotent and safe
# to run on every build - it's core/ source (compiled fresh each build), not a
# prebuilt lib, and this file is shared by every board's env.
def _patch_hosted_sdio_freq():
    platform = env.PioPlatform()
    framework_dir = platform.get_package_dir("framework-arduinoespressif32")
    if not framework_dir:
        return
    target = os.path.join(framework_dir, "cores", "esp32", "esp32-hal-hosted.c")
    if not os.path.isfile(target):
        return

    with open(target, "r", encoding="utf-8") as f:
        content = f.read()

    if "LAUNCHER_HOSTED_SDIO_FREQ_PATCH" in content:
        return  # already patched

    marker = "struct esp_hosted_sdio_config conf = INIT_DEFAULT_HOST_SDIO_CONFIG();"
    if marker not in content:
        print("[patch_hosted_sdio_freq] marker not found, skipping (upstream file changed?)")
        return

    replacement = (
        marker
        + "\n#ifdef LAUNCHER_HOSTED_SDIO_FREQ_KHZ\n"
        + "  conf.clock_freq_khz = LAUNCHER_HOSTED_SDIO_FREQ_KHZ; // LAUNCHER_HOSTED_SDIO_FREQ_PATCH\n"
        + "#endif"
    )
    content = content.replace(marker, replacement, 1)
    with open(target, "w", encoding="utf-8") as f:
        f.write(content)
    print("[patch_hosted_sdio_freq] patched esp32-hal-hosted.c")


_patch_hosted_sdio_freq()
