import os

Import("env")   # type: ignore

def set_nvs():
    flag_path = os.path.join(env['PROJECT_DIR'], '.pio', 'build', env['PIOENV'], 'nvs_flag.txt')
    os.makedirs(os.path.dirname(flag_path), exist_ok=True)
    with open(flag_path, 'w') as f:
        f.write("NVS flag created!\n")
    print(f"[INFO] NVS Flag created at: {flag_path}")

def before_upload(source, target, env):
    bin_path = os.path.join(env['PROJECT_DIR'], 'support_files', 'UiFlow2_nvs.bin')
    print("[NVS file] NVS flag set to upload")
    env.Append(UPLOADERFLAGS=[0x9000, bin_path])

env.AddPreAction("upload", before_upload)
set_nvs()
