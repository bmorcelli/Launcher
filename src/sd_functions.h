#ifndef __SD_FUNCTIONS_H
#define __SD_FUNCTIONS_H
#include <globals.h>

#include "idf/idf_update.h"
#include <SPI.h>

#include <FFat.h>
#include <FS.h>
#include <SD.h>
#if !defined(SDM_SD)
#include <SD_MMC.h>
#endif
extern SPIClass sdcardSPI;

bool setupSdCard();

bool deleteFromSd(const String &path);

bool renameFile(const String &path, const String &filename);

bool copyFile(const String &path);

bool pasteFile(const String &path);

bool createFolder(String path);

void readFs(String &folder, std::vector<Option> &opt);

bool sortList(const Option &a, const Option &b);

String loopSD(bool filePicker = false);

void updateFromSD(const String &path);

bool performDATAUpdate(Stream &updateSource, size_t updateSize, const char *label);

#endif
