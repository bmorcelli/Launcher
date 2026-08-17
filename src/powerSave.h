#ifndef __POWERSAVE_H
#define __POWERSAVE_H

#include "display.h"
#include <globals.h>

// It is here to not create a new set of files just to use AXP192 library again
void turnOffDisplay();

bool wakeUpScreen();

void checkPowerSaveTime();

void sleepModeOn();

void sleepModeOff();

#endif