// Configuration hook for the DisplayDrivers library.
//
// The library's own src/DisplayDrivers_Setup.h reads this file first (it is
// found through `build_flags = -I include`), before anything is decided, so
// whatever lands here reaches every translation unit — including the library's
// own .cpp files, which never see macros defined in the project's sources.
// The name matters: a DisplayDrivers_Setup.h here would be shadowed by the
// library's copy of that name, which sits earlier on the include path.
//
// Everything that describes a panel lives in boards/<board>/platformio.ini in
// the library's own vocabulary (USE_*, TFT_DATABUS_N, TFT_DISPLAY_DRIVER_N,
// TFT_* pins). This file only carries over the project-wide defaults that the
// boards rely on but never spell out.
#ifndef _LAUNCHER_DISPLAYDRIVERS_USER_SETUP_H
#define _LAUNCHER_DISPLAYDRIVERS_USER_SETUP_H

// TFT_WIDTH / TFT_HEIGHT / TFT_MISO / ROTATION and the CYD pin fallbacks. The
// backends need them as much as the launcher's own sources do.
#include "pre_compiler.h"

// The launcher calls the panel's mounting rotation ROTATION (it is also the
// default the user can change at runtime); DisplayDrivers calls it
// TFT_ROTATION. Same number, so bridge it here instead of duplicating it in
// forty board files.
#ifndef TFT_ROTATION
#define TFT_ROTATION ROTATION
#endif

// GXEPD2_PANEL has to name a class the library's own translation units can
// see, and GxEPD2_BW.h only pulls in the panels that ship with GxEPD2. The
// Xteink X3's is not one of them, so its board folder carries the class and
// this is the only place early enough to introduce it. The env sets both the
// flag and -I boards/xteink; no other board is affected.
#if defined(DISPLAY_XTEINK_X3)
#include "GxEPD2_X3_792x528.h"
#endif

#endif // _LAUNCHER_DISPLAYDRIVERS_USER_SETUP_H
