Import("env")   # type: ignore

def before_upload(source, target, env):
    # The factory-safe routing is PaperMono-only. Other environments must
    # retain the PlatformIO/pioarduino-selected application partition.
    if env.subst("${PIOENV}") == "m5stack-paper-mono":
        env.Replace(ESP32_APP_OFFSET="0x10000")

env.AddPreAction("upload", before_upload)
