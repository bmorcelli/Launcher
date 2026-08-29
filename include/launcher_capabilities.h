#pragma once

#ifndef LAUNCHER_FIRMWARE_INSTALL_ALLOWED
#define LAUNCHER_FIRMWARE_INSTALL_ALLOWED 1
#endif

#ifndef LAUNCHER_FIRMWARE_INSPECT_ALLOWED
#define LAUNCHER_FIRMWARE_INSPECT_ALLOWED 0
#endif

#ifndef LAUNCHER_RAW_FLASH_ALLOWED
#define LAUNCHER_RAW_FLASH_ALLOWED LAUNCHER_FIRMWARE_INSTALL_ALLOWED
#endif

#ifndef LAUNCHER_RUNTIME_PARTITION_MUTATION_ALLOWED
#define LAUNCHER_RUNTIME_PARTITION_MUTATION_ALLOWED LAUNCHER_FIRMWARE_INSTALL_ALLOWED
#endif

#ifndef LAUNCHER_APP_BOOT_SELECT_ALLOWED
#define LAUNCHER_APP_BOOT_SELECT_ALLOWED 0
#endif

constexpr bool launcherFirmwareInstallAllowed() { return LAUNCHER_FIRMWARE_INSTALL_ALLOWED != 0; }
constexpr bool launcherFirmwareInspectAllowed() { return LAUNCHER_FIRMWARE_INSPECT_ALLOWED != 0; }
constexpr bool launcherRawFlashAllowed() { return LAUNCHER_RAW_FLASH_ALLOWED != 0; }
constexpr bool launcherRuntimePartitionMutationAllowed() {
    return LAUNCHER_RUNTIME_PARTITION_MUTATION_ALLOWED != 0;
}
constexpr bool launcherAppBootSelectAllowed() { return LAUNCHER_APP_BOOT_SELECT_ALLOWED != 0; }
constexpr bool launcherFirmwareMutationAllowed() {
    return launcherFirmwareInstallAllowed() && launcherRawFlashAllowed() &&
           launcherRuntimePartitionMutationAllowed();
}
