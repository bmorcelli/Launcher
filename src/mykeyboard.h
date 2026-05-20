#ifndef __KEYBOARD_H
#define __KEYBOARD_H
#include <WString.h>
#include <globals.h>

String keyboard(String mytext, int maxSize = 76, String msg = "Type your message:");

keyStroke _getKeyPress(); // return the key pressed to use as shortcut or input in keyboard environment
                          // by using the flag HAS_KEYBOARD

void __attribute__((weak)) powerOff();

void __attribute__((weak)) reboot();

void __attribute__((weak)) checkReboot();

// It is here to not create a new set of files just to use AXP192 library again
int __attribute__((weak)) getBattery();
#endif
