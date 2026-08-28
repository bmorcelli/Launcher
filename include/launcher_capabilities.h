#pragma once

#ifndef LAUNCHER_FIRMWARE_INSTALL_ALLOWED
#define LAUNCHER_FIRMWARE_INSTALL_ALLOWED 1
#endif

#ifndef LAUNCHER_FIRMWARE_INSPECT_ALLOWED
#define LAUNCHER_FIRMWARE_INSPECT_ALLOWED 0
#endif

constexpr bool launcherFirmwareInstallAllowed() { return LAUNCHER_FIRMWARE_INSTALL_ALLOWED != 0; }
constexpr bool launcherFirmwareInspectAllowed() { return LAUNCHER_FIRMWARE_INSPECT_ALLOWED != 0; }
