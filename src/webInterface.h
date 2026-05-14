#ifndef __WEBINTERFACE_H
#define __WEBINTERFACE_H

#include <FS.h>
#include <SPI.h>
#include <webFiles.h>

String humanReadableSize(uint64_t bytes);
String listFiles(String folder);
String readLineFromFile(File myFile);

void configureWebServer();
void startWebUi(String ssid, int encryptation, bool mode_ap = false);

void webUIMyNet();
void loopOptionsWebUi();

#endif //__WEBINTERFACE_H
