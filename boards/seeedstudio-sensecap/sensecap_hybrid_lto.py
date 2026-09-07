Import("env")

if env.subst("$ARDUINO_LIB_COMPILE_FLAG") == "Build":
    for key in ("CFLAGS", "CCFLAGS", "CXXFLAGS", "ASFLAGS", "LINKFLAGS"):
        flags = env.get(key, [])
        if isinstance(flags, str):
            flags = flags.split()
        env.Replace(**{key: [flag for flag in flags if not str(flag).startswith("-flto")]})

    env.AppendUnique(
        CFLAGS=["-fno-lto"],
        CCFLAGS=["-fno-lto"],
        CXXFLAGS=["-fno-lto"],
        ASFLAGS=["-fno-lto"],
        LINKFLAGS=["-fno-lto"],
    )
