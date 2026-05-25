Import("env")
import os
import shutil


def after_build(source, target, **kwargs):
    env_ctx = kwargs["env"]
    pio_env = env_ctx.subst("$PIOENV")
    build_dir = env_ctx.subst("$BUILD_DIR")
    project_dir = env_ctx.subst("$PROJECT_DIR")

    root_dir = os.path.abspath(os.path.join(project_dir, "..", ".."))
    dest_dir = os.path.join(root_dir, "src", "assets", pio_env)
    os.makedirs(dest_dir, exist_ok=True)

    for filename in ("bootloader.bin", "partitions.bin"):
        src = os.path.join(build_dir, filename)
        dst = os.path.join(dest_dir, filename)
        if os.path.isfile(src):
            shutil.copy2(src, dst)
            print(f"[binMaker] {filename} -> {dst}")
        else:
            print(f"[binMaker] WARNING: {src} not found, skipping")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)
