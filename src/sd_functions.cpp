#include "sd_functions.h"
#include "app_registry.h"
#include "display.h"
#include "esp_log.h"
#include "idf/idf_update.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "partition_table_model.h"
#include "settings.h"
#include <algorithm>
#include <esp_app_format.h>
#include <esp_flash.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <globals.h>
#include <memory>
SPIClass sdcardSPI;
String fileToCopy;
String fileToUse;

bool setupSdCard() {
#if !defined(SDM_SD) // fot Lilygo T-Display S3 with lilygo shield
#if defined(USE_SD_MMC) && defined(PIN_SD_CLK) && defined(PIN_SD_CMD) && defined(PIN_SD_D0)
    SD_MMC.end();
    vTaskDelay(pdTICKS_TO_MS(20));
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    vTaskDelay(pdTICKS_TO_MS(10));
#else
#endif
    if (!SD_MMC.begin("/sdcard", true, false)) // One bit mode, don't auto-format
#elif (TFT_MOSI == SDCARD_MOSI)
    if (!SDM.begin(_cs)) // https://github.com/Bodmer/TFT_eSPI/discussions/2420
#elif defined(HEADLESS)
    if (_sck == 0 && _miso == 0 && _mosi == 0 && _cs == 0) {
        launcherConsolePrintln("SdCard pins not set");
        return false;
    }

    sdcardSPI.begin(_sck, _miso, _mosi, _cs); // start SPI communications
    vTaskDelay(pdTICKS_TO_MS(10));
    if (!SDM.begin(_cs, sdcardSPI))
#elif defined(DONT_USE_INPUT_TASK)
#if (TFT_MOSI != SDCARD_MOSI)
    sdcardSPI.begin(_sck, _miso, _mosi, _cs); // start SPI communications
    if (!SDM.begin(_cs, sdcardSPI))
#else
    if (!SDM.begin(_cs))
#endif

#else
    sdcardSPI.begin(_sck, _miso, _mosi, _cs); // start SPI communications
    vTaskDelay(pdTICKS_TO_MS(10));
    if (!SDM.begin(_cs, sdcardSPI))
#endif
    {
        // sdcardSPI.end(); // Closes SPI connections and release pin header.
        launcherConsolePrintln("Failed to mount SDCARD");
        sdcardMounted = false;
        return false;
    } else {
        launcherConsolePrintln("SDCARD mounted successfully");
        sdcardMounted = true;
        return true;
    }
}

/***************************************************************************************
** Function name: closeSdCard
** Description:   Turn Off SDCard, set sdcardMounted state to false
***************************************************************************************/
void closeSdCard() {
    SDM.end();
    sdcardMounted = false;
}

/***************************************************************************************
** Function name: deleteFromSd
** Description:   delete file or folder
***************************************************************************************/
bool deleteFromSd(String path) {
    File dir = SDM.open(path);
    if (!dir.isDirectory()) { return SDM.remove(path.c_str()); }

    dir.rewindDirectory();
    bool success = true;

    bool isDir;
    String fileName = dir.getNextFileName(&isDir);
    while (fileName != "") {
        String fullPath = path + "/" + fileName;
        if (isDir) {
            success &= deleteFromSd(fullPath);
        } else {
            success &= SDM.remove(fullPath.c_str());
        }
        fileName = dir.getNextFileName(&isDir);
    }

    dir.close();
    // Apaga a própria pasta depois de apagar seu conteúdo
    success &= SDM.rmdir(path.c_str());
    return success;
}

/***************************************************************************************
** Function name: renameFile
** Description:   rename file or folder
***************************************************************************************/
bool renameFile(String path, String filename) {
    String newName = keyboard(filename, 76, "Type the new Name:");
    if (newName == "" || newName == String(KEY_ESCAPE) || newName == filename) { return false; }
    if (!setupSdCard()) {
        // Serial.println("Falha ao inicializar o cartão SD");
        return false;
    }

    // Rename the file of folder
    if (SDM.rename(path, path.substring(0, path.lastIndexOf('/')) + "/" + newName)) {
        // Serial.println("Renamed from " + filename + " to " + newName);
        return true;
    } else {
        // Serial.println("Fail on rename.");
        return false;
    }
}

/***************************************************************************************
** Function name: copyFile
** Description:   copy file address to memory
***************************************************************************************/
bool copyFile(String path) {
    if (!setupSdCard()) {
        // Serial.println("Fail to start SDCard");
        return false;
    }
    File file = SDM.open(path, FILE_READ);
    if (!file.isDirectory()) {
        fileToCopy = path;
        file.close();
        return true;
    } else {
        displayRedStripe("Cannot copy Folder");
        launcherDelayMs(2000);
        file.close();
        return false;
    }
}

/***************************************************************************************
** Function name: pasteFile
** Description:   paste file to new folder
***************************************************************************************/
bool pasteFile(String path) {
    // Tamanho do buffer para leitura/escrita
    const size_t bufferSize = 2048 * 2; // Ajuste conforme necessário para otimizar a performance
    uint8_t buffer[bufferSize];

    // Abrir o arquivo original
    File sourceFile = SDM.open(fileToCopy, FILE_READ);
    if (!sourceFile) {
        // Serial.println("Falha ao abrir o arquivo original para leitura");
        return false;
    }

    // Criar o arquivo de destino
    File destFile =
        SDM.open(path + "/" + fileToCopy.substring(fileToCopy.lastIndexOf('/') + 1), FILE_WRITE, true);
    if (!destFile) {
        // Serial.println("Falha ao criar o arquivo de destino");
        sourceFile.close();
        return false;
    }

    // Ler dados do arquivo original e escrever no arquivo de destino
    size_t bytesRead;
    int tot = sourceFile.size();
    int prog = 0;
    // tft->drawRect(5,tftHeight-12, (tftWidth-10), 9, FGCOLOR);
    while ((bytesRead = sourceFile.read(buffer, bufferSize)) > 0) {
        if (destFile.write(buffer, bytesRead) != bytesRead) {
            // Serial.println("Falha ao escrever no arquivo de destino");
            sourceFile.close();
            destFile.close();
            return false;
        } else {
            prog += bytesRead;
            float rad = 360 * prog / tot;
            tft->drawArc(tftWidth / 2, tftHeight / 2, tftHeight / 4, tftHeight / 5, 0, int(rad), ALCOLOR);
            // tft->fillRect(7,tftHeight-10, (tftWidth-14)*prog/tot, 5, FGCOLOR);
        }
    }

    // Fechar ambos os arquivos
    sourceFile.close();
    destFile.close();
    return true;
}

/***************************************************************************************
** Function name: createFolder
** Description:   create new folder
***************************************************************************************/
bool createFolder(String path) {
    String foldername = keyboard("", 76, "Folder Name: ");
    if (foldername == "" || foldername == String(KEY_ESCAPE)) { return false; }
    if (!setupSdCard()) {
        // Serial.println("Fail to start SDCard");
        return false;
    }
    if (path != "/") path += "/";
    if (!SDM.mkdir(path + foldername)) {
        displayRedStripe("Couldn't create folder");
        launcherDelayMs(2000);
        return false;
    }
    return true;
}

/***************************************************************************************
** Function name: sortList
** Description:   sort files/folders by name
***************************************************************************************/
bool sortList(const Option &a, const Option &b) {
    const uint16_t _folderColor = uint16_t(FGCOLOR - 0x1111);
    bool _a = (a.color == _folderColor); // is folder
    bool _b = (b.color == _folderColor); // is folder
    if (_a != _b) {
        return _a > _b; // true if a is a folder and b is not
    }
    // Order items alphabetically
    String fa = a.label;
    fa.toUpperCase();
    String fb = b.label;
    fb.toUpperCase();
    return fa < fb;
}

/***************************************************************************************
** Function name: readFs
** Description:   read files/folders from a folder
***************************************************************************************/
void readFs(String &folder, std::vector<Option> &opt) {
    // function using loopOptions
    opt.clear();
    if (!setupSdCard()) {
        // Serial.println("Falha ao iniciar o cartão SD");
        displayRedStripe("SD not found or not formatted in FAT32");
        vTaskDelay(2500 / portTICK_PERIOD_MS);
        return; // Retornar imediatamente em caso de falha
    }
    File root = SDM.open(folder);
    if (!root || !root.isDirectory()) {
        displayRedStripe("Fail open root");
        vTaskDelay(2500 / portTICK_PERIOD_MS);
        SDM.end();
        sdcardMounted = false;
        return; // Retornar imediatamente se não for possível abrir o diretório
    }

    while (true) {
        bool isDir;
        String fullPath = root.getNextFileName(&isDir);
        String nameOnly = fullPath.substring(fullPath.lastIndexOf("/") + 1);
        if (fullPath == "") { break; }
        // Serial.printf("Path: %s (isDir: %d)\n", fullPath.c_str(), isDir);

        uint16_t color = FGCOLOR - 0x1111;

        if (noDotFiles && nameOnly.startsWith(".")) { continue; }

        if (!isDir) {
            int dotIndex = nameOnly.lastIndexOf(".");
            String ext = dotIndex >= 0 ? nameOnly.substring(dotIndex + 1) : "";
            ext.toUpperCase();
            if (onlyBins && !ext.equals("BIN")) { continue; }
            color = FGCOLOR;
        } else {
            nameOnly = "/" + nameOnly; // add / before folder name
        }
        opt.push_back({nameOnly, [fullPath]() { fileToUse = fullPath; }, color});
    }
    root.close();
    std::sort(opt.begin(), opt.end(), sortList);
    opt.push_back({"> Back", [&]() { fileToUse = ""; }, ALCOLOR});
}
/*********************************************************************
**  Function: loopSD
**  Where you choose what to do wuth your SD Files
**********************************************************************/
String loopSD(bool filePicker) {
    // Function using loopOptions to store and handle files
    returnToMenu = false;
    fileToUse = ""; // resets global variable
    int index = 0;
    int Menuindex = 0;
    String Folder = "/";
    String _Folder = ""; // Check if Folder changed
    String PreFolder = "/";
    bool isFolder = false;
    bool isOperator = false;
    bool LongPressDetected = false;
    bool read_fs = true;
    bool bkf = false;
RESTART:
    if (_Folder != Folder || read_fs) {
        readFs(Folder, options);
        if (options.size() == 0) return ""; // Failed reading SD card.
        _Folder = Folder;
        index = 0;
        bkf = false;
        read_fs = false;
    }
    index = loopOptions(options, false, FGCOLOR, BGCOLOR, false, index);
    // First Exit
    if (index < 0) goto BACK_FOLDER;
    // Check if it is Folder or operator (> Back)
    if (options[index].color == uint16_t(FGCOLOR - 0x1111)) isFolder = true;
    else isFolder = false;
    if (options[index].color == uint16_t(ALCOLOR)) isOperator = true;
    else isOperator = false;
    if (filePicker && !isFolder && !isOperator) return fileToUse;

    // Long Press Detection
    LongPressDetected = false;
#ifndef E_PAPER_DISPLAY
    LongPress = true;
    SelPress = true; // it was just pressed
    LongPressTmp = launcherMillis();
    while (launcherMillis() - LongPressTmp < 300 && SelPress) {
        check(AnyKeyPress);
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    if (check(SelPress)) LongPressDetected = true;
    LongPress = false;
    SelPress = false;
#else
    // Always behave as if it was long pressed
    // But shows Option to enter on folders
    LongPressDetected = true;
#endif
    // Menu for if it is a Folder
    if (isFolder) {
        // Short press on folder opens the folder
        if (!LongPressDetected) {
            PreFolder = Folder;
            Folder = fileToUse;
            launcherConsolePrintf(
                "Going : Folder    = %s\nPreFolder = %s\n", Folder.c_str(), PreFolder.c_str()
            );
            goto RESTART;
        }

        std::vector<Option> opt = {
#ifdef E_PAPER_DISPLAY
            {"Open Folder", [&]() { Folder = fileToUse; }                         },
#endif
            {"New Folder",  [=]() { createFolder(Folder); }                       },
            {"Rename",      [=]() { renameFile(fileToUse, options[index].label); }},
            {"Delete",      [=]() { deleteFromSd(fileToUse); }                    },
            {"Main Menu",   [=]() { returnToMenu = true; }                        },
        };
        Menuindex = loopOptions(opt);
        // Menu for if it is an Operator
    } else if (isOperator) {
        if (LongPressDetected) {
            bkf = false;
            std::vector<Option> opt = {
#ifdef E_PAPER_DISPLAY
                {"Back Folder", [&]() { bkf = true; }          },
#endif
                {"New Folder",  [=]() { createFolder(Folder); }},
            };
            if (fileToCopy != "") opt.push_back({"Paste", [=]() { pasteFile(Folder); }});
            opt.push_back({"Main Menu", [=]() { returnToMenu = true; }});
            Menuindex = loopOptions(opt);
        }
        if (bkf || fileToUse == "") {
        BACK_FOLDER:
            Folder = PreFolder;
            if (PreFolder != "/") PreFolder = PreFolder.substring(0, PreFolder.lastIndexOf('/'));
            if (PreFolder == "") PreFolder = "/";
            if (_Folder == PreFolder) returnToMenu = true;
            launcherConsolePrintf(
                "Backing: Folder    = %s\nPreFolder = %s\n", Folder.c_str(), PreFolder.c_str()
            );
        }
    } else {
        std::vector<Option> opt = {
            {"Install",    [=]() { updateFromSD(fileToUse); }                    },
            {"New Folder", [=]() { createFolder(Folder); }                       },
            {"Rename",     [=]() { renameFile(fileToUse, options[index].label); }},
            {"Copy",       [=]() { copyFile(fileToUse); }                        },
        };
        if (fileToCopy != "") opt.push_back({"Paste", [=]() { pasteFile(Folder); }});
        opt.push_back({"Delete", [=]() { deleteFromSd(fileToUse); }});
        opt.push_back({"Main Menu", [=]() { returnToMenu = true; }});
        Menuindex = loopOptions(opt);
    }
    if (Menuindex >= 0) read_fs = true;
    if (!returnToMenu) goto RESTART;
    // Free the memory
    options.clear();
    tft->fillScreen(BGCOLOR);
    return fileToUse;
}

/***************************************************************************************
** Function name: performUpdate
** Description:   this function performs the update
***************************************************************************************/
bool performUpdate(Stream &updateSource, size_t updateSize, int command) {
    bool success = false;
    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
    progressHandler(0, 500);

    vTaskSuspend(xHandle);
    LauncherUpdateTarget target;
    if (launcherUpdateTargetFromCommand(command, target) && launcherUpdateBegin(target, updateSize)) {
        size_t written = 0;
        uint8_t buf[1024];

        prog_handler = target == LAUNCHER_UPDATE_APP ? 0 : 1;
        log_i("updateSize = %d", updateSize);
        while (written < updateSize) {
            size_t toRead = min(sizeof(buf), updateSize - written);
            int bytesRead = updateSource.readBytes(buf, toRead);
            if (bytesRead <= 0) {
                launcherUpdateAbort();
                break;
            }
            size_t bytesWritten = launcherUpdateWrite(buf, bytesRead);
            if (bytesWritten != static_cast<size_t>(bytesRead)) break;
            written += bytesWritten;
            progressHandler(written, updateSize);
        }
        if (launcherUpdateEnd()) {
            if (launcherUpdateIsFinished()) {
                log_i("Update successfully completed. Rebooting.");
                displayRedStripe("Post Install Cleanup");
                launcherClearCoredump();
                success = true;
            } else log_i("Update not finished? Something went wrong!");
        } else {
            log_i("Error Occurred. Error #: %d", launcherUpdateLastError());
        }
    } else {
        uint8_t error = launcherUpdateLastError();
        displayRedStripe("E:" + String(error) + "-Wrong Partition Scheme");
        launcherDelayMs(2500);
    }
    vTaskResume(xHandle);
    return success;
}

static String installedAppNameFromPath(const String &path) { return launcherAppNameFromFile(path); }

static String sanitizedSdAppLabelBase(const String &name) {
    String base;
    for (size_t i = 0; i < name.length() && base.length() < 6; ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) base += c;
    }
    if (base.isEmpty()) base = "app";
    while (base.length() < 6) base += "0";
    return base;
}

static bool sdPartitionLabelExists(const LauncherPartitionTable &table, const String &label) {
    for (const LauncherPartitionEntry &entry : table.entries) {
        if (label == entry.label) return true;
    }
    return false;
}

static String nextSdAppLabel(const LauncherPartitionTable &table, const String &installedName) {
    String base = sanitizedSdAppLabelBase(installedName);
    if (!sdPartitionLabelExists(table, base)) return base;

    String prefix = base.substring(0, 5);
    for (int i = 1; i <= 9; ++i) {
        String candidate = prefix + String(i);
        if (!sdPartitionLabelExists(table, candidate)) return candidate;
    }
    String candidate = prefix + "0";
    if (!sdPartitionLabelExists(table, candidate)) return candidate;

    for (int i = 1; i < 100; ++i) {
        candidate = "app" + String(i);
        if (!sdPartitionLabelExists(table, candidate)) return candidate;
    }
    return "app";
}

static bool renameSdEntryByOffset(LauncherPartitionTable &table, uint32_t offset, const String &label) {
    for (LauncherPartitionEntry &entry : table.entries) {
        if (entry.offset != offset) continue;
        memset(entry.label, 0, sizeof(entry.label));
        strncpy(entry.label, label.c_str(), sizeof(entry.label) - 1);
        return true;
    }
    return false;
}

static bool findOrCreateSdDataPartition(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, uint32_t requestedSize,
    LauncherPartitionEntry &entry, String &error
) {
    LauncherPartitionEntry *existing = launcherPartitionFindByLabel(table, label);
    if (existing) {
        if (existing->isData() && existing->subtype == subtype && existing->size >= requestedSize) {
            entry = *existing;
            return true;
        }
        error = String("Partition ") + label + " is too small or incompatible";
        return false;
    }
    return launcherPartitionCreateData(table, subtype, label, requestedSize, &entry, &error);
}

static uint32_t alignSdUp(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

static uint32_t sdDefaultSpiffsSize() { return 0x70000; }

static uint32_t sdLargeSpiffsThreshold() { return 0x500000; }

static uint32_t sdUseRemainingSpiffsSize() { return 0xFFFFFFFF; }

static bool createSdDataInLargestFreeRange(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, LauncherPartitionEntry &entry,
    String &error
) {
    LauncherPartitionRange best;
    for (const LauncherPartitionRange &range : launcherPartitionFreeRanges(table)) {
        const uint32_t alignedOffset = alignSdUp(range.offset, LAUNCHER_FLASH_SECTOR_SIZE);
        if (alignedOffset < range.offset || range.size < alignedOffset - range.offset) continue;
        const uint32_t alignedSize =
            (range.size - (alignedOffset - range.offset)) & ~(LAUNCHER_FLASH_SECTOR_SIZE - 1);
        if (alignedSize > best.size) best = {alignedOffset, alignedSize};
    }
    if (best.size == 0) {
        error = "No free partition range large enough";
        return false;
    }

    LauncherPartitionEntry created;
    created.type = 0x01;
    created.subtype = subtype;
    created.offset = best.offset;
    created.size = best.size;
    created.flags = 0;
    memset(created.label, 0, sizeof(created.label));
    strncpy(created.label, label, sizeof(created.label) - 1);
    if (!launcherPartitionAdd(table, created, &error)) return false;
    entry = created;
    return true;
}

static String sdHexSize(uint32_t value) {
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "0x%06lX", static_cast<unsigned long>(value));
    return String(buffer);
}

static String sdHumanSize(uint32_t value) {
    if (value >= 1024 * 1024 && value % (1024 * 1024) == 0) { return String(value / (1024 * 1024)) + "MB"; }
    if (value >= 1024 && value % 1024 == 0) { return String(value / 1024) + "KB"; }
    return String(value) + " bytes";
}

static String sdSizeLabel(uint32_t value) { return sdHexSize(value) + " (" + sdHumanSize(value) + ")"; }

static bool removeSdEntryByOffset(LauncherPartitionTable &table, uint32_t offset) {
    for (auto it = table.entries.begin(); it != table.entries.end(); ++it) {
        if (it->offset == offset) {
            table.entries.erase(it);
            return true;
        }
    }
    return false;
}

static bool isReplaceableSdApp(const LauncherPartitionEntry &entry) {
    if (!entry.isOtaApp()) return false;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->address == entry.offset) return false;
    return true;
}

static bool isRemovableSdInstallData(const LauncherPartitionEntry &entry) {
    if (!entry.isData()) return false;
    if (entry.subtype != 0x81 && entry.subtype != 0x82 && entry.subtype != 0x83) return false;
    return strcmp(entry.label, "spiffs") == 0 || strcmp(entry.label, "sys") == 0 ||
           strcmp(entry.label, "vfs") == 0;
}

static bool removeSdInstallDataPartitions(LauncherPartitionTable &table, bool removeSpiffs) {
    bool removed = false;
    for (auto it = table.entries.begin(); it != table.entries.end();) {
        bool removable = isRemovableSdInstallData(*it);
        if (removable && !removeSpiffs && strcmp(it->label, "spiffs") == 0) removable = false;
        if (removable) {
            it = table.entries.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    return removed;
}

static bool addManualSdAppEntry(
    LauncherPartitionTable &table, uint8_t subtype, const char *label, uint32_t offset, uint32_t size,
    LauncherPartitionEntry &created, String &error
) {
    LauncherPartitionEntry entry;
    entry.type = 0x00;
    entry.subtype = subtype;
    entry.offset = offset;
    entry.size = alignSdUp(size, LAUNCHER_APP_PARTITION_ALIGNMENT);
    entry.flags = 0;
    memset(entry.label, 0, sizeof(entry.label));
    strncpy(entry.label, label, sizeof(entry.label) - 1);
    if (!launcherPartitionAdd(table, entry, &error)) return false;
    created = entry;
    return true;
}

static bool prepareSdDataPartitions(
    LauncherPartitionTable &table, bool spiffs, uint32_t spiffsSize, LauncherPartitionEntry &spiffsEntry,
    bool &hasSpiffsEntry, bool fat, uint32_t fatSizeSys, const char *fatLabelSys, uint32_t fatSizeVfs,
    const char *fatLabelVfs, LauncherPartitionEntry &fatSysEntry, bool &hasFatSys,
    LauncherPartitionEntry &fatVfsEntry, bool &hasFatVfs, String &error
) {
    hasSpiffsEntry = false;
    hasFatSys = false;
    hasFatVfs = false;

    auto prepareSpiffs = [&]() {
        LauncherPartitionEntry *existing = launcherPartitionFindByLabel(table, "spiffs");
        if (existing) {
            if (!existing->isData() || existing->subtype != 0x82) {
                error = "Partition spiffs is incompatible";
                return false;
            }
            if (spiffsSize == sdUseRemainingSpiffsSize()) {
                uint32_t oldOffset = existing->offset;
                if (!removeSdEntryByOffset(table, oldOffset)) {
                    error = "Could not resize spiffs partition";
                    return false;
                }
                if (!createSdDataInLargestFreeRange(table, 0x82, "spiffs", spiffsEntry, error)) return false;
                return true;
            }
            if (spiffsSize != sdUseRemainingSpiffsSize() && existing->size < spiffsSize) {
                error = "Partition spiffs is too small or incompatible";
                return false;
            }
            spiffsEntry = *existing;
        } else if (spiffsSize == sdUseRemainingSpiffsSize()) {
            if (!createSdDataInLargestFreeRange(table, 0x82, "spiffs", spiffsEntry, error)) return false;
        } else {
            if (!findOrCreateSdDataPartition(table, 0x82, "spiffs", spiffsSize, spiffsEntry, error))
                return false;
        }
        hasSpiffsEntry = true;
        return true;
    };

    if (spiffs && spiffsSize > 0 && spiffsSize != sdUseRemainingSpiffsSize()) {
        if (!prepareSpiffs()) return false;
    }

    if (fat && fatSizeSys > 0) {
        const char *label = fatLabelSys && fatLabelSys[0] ? fatLabelSys : "sys";
        uint32_t desired = launcherPartitionDefaultFatSize(label);
        if (desired < fatSizeSys) desired = fatSizeSys;
        if (!findOrCreateSdDataPartition(table, 0x81, label, desired, fatSysEntry, error)) return false;
        hasFatSys = true;
    }

    if (fat && fatSizeVfs > 0) {
        const char *label = fatLabelVfs && fatLabelVfs[0] ? fatLabelVfs : "vfs";
        uint32_t desired = launcherPartitionDefaultFatSize(label);
        if (desired < fatSizeVfs) desired = fatSizeVfs;
        if (!findOrCreateSdDataPartition(table, 0x81, label, desired, fatVfsEntry, error)) return false;
        hasFatVfs = true;
    }

    if (spiffs && spiffsSize == sdUseRemainingSpiffsSize()) {
        if (!prepareSpiffs()) return false;
    }

    return true;
}

static bool selectSdInstallLayout(
    LauncherPartitionTable &table, uint32_t appSize, const String &defaultLabel, bool spiffs,
    uint32_t spiffsSize, bool fat, uint32_t fatSizeSys, const char *fatLabelSys, uint32_t fatSizeVfs,
    const char *fatLabelVfs, LauncherPartitionEntry &appEntry, LauncherPartitionEntry &spiffsEntry,
    bool &hasSpiffsEntry, LauncherPartitionEntry &fatSysEntry, bool &hasFatSys,
    LauncherPartitionEntry &fatVfsEntry, bool &hasFatVfs, String &error
) {
    const uint32_t requiredAppPartitionSize = alignSdUp(appSize, LAUNCHER_APP_PARTITION_ALIGNMENT);
    uint32_t requiredInstallSize = requiredAppPartitionSize;
    if (fat) requiredInstallSize += fatSizeSys + fatSizeVfs;
    if (spiffs && spiffsSize > 0 && spiffsSize != sdUseRemainingSpiffsSize()) {
        requiredInstallSize += spiffsSize;
    }
    LauncherPartitionTable directCandidate = table;
    LauncherPartitionEntry directApp;
    LauncherPartitionEntry directSpiffs;
    LauncherPartitionEntry directFatSys;
    LauncherPartitionEntry directFatVfs;
    bool directHasSpiffs = false;
    bool directHasFatSys = false;
    bool directHasFatVfs = false;
    if (launcherPartitionCreateOtaApp(directCandidate, appSize, defaultLabel.c_str(), &directApp, &error) &&
        prepareSdDataPartitions(
            directCandidate,
            spiffs,
            spiffsSize,
            directSpiffs,
            directHasSpiffs,
            fat,
            fatSizeSys,
            fatLabelSys,
            fatSizeVfs,
            fatLabelVfs,
            directFatSys,
            directHasFatSys,
            directFatVfs,
            directHasFatVfs,
            error
        ) &&
        launcherPartitionValidate(directCandidate, &error)) {
        table = directCandidate;
        appEntry = directApp;
        spiffsEntry = directSpiffs;
        hasSpiffsEntry = directHasSpiffs;
        fatSysEntry = directFatSys;
        hasFatSys = directHasFatSys;
        fatVfsEntry = directFatVfs;
        hasFatVfs = directHasFatVfs;
        return true;
    }

    LauncherPartitionTable original = table;
    std::vector<Option> choices;
    std::vector<String> choiceLabels;
    choices.push_back({String("Need ") + sdSizeLabel(requiredInstallSize), []() {}});
    choiceLabels.push_back(choices.back().label);

    auto addChoice = [&](const String &label,
                         const LauncherPartitionTable &candidate,
                         const LauncherPartitionEntry &candidateApp,
                         const LauncherPartitionEntry &candidateSpiffs,
                         bool candidateHasSpiffs,
                         const LauncherPartitionEntry &candidateFatSys,
                         bool candidateHasFatSys,
                         const LauncherPartitionEntry &candidateFatVfs,
                         bool candidateHasFatVfs) {
        for (const String &existingLabel : choiceLabels) {
            if (existingLabel == label) return;
        }
        choiceLabels.push_back(label);
        choices.push_back(
            {label,
             [&table,
              &appEntry,
              &spiffsEntry,
              &hasSpiffsEntry,
              &fatSysEntry,
              &hasFatSys,
              &fatVfsEntry,
              &hasFatVfs,
              candidate,
              candidateApp,
              candidateSpiffs,
              candidateHasSpiffs,
              candidateFatSys,
              candidateHasFatSys,
              candidateFatVfs,
              candidateHasFatVfs]() mutable {
                 table = candidate;
                 appEntry = candidateApp;
                 spiffsEntry = candidateSpiffs;
                 hasSpiffsEntry = candidateHasSpiffs;
                 fatSysEntry = candidateFatSys;
                 hasFatSys = candidateHasFatSys;
                 fatVfsEntry = candidateFatVfs;
                 hasFatVfs = candidateHasFatVfs;
             }}
        );
    };

    auto addAutoLayoutChoice = [&](const String &label, LauncherPartitionTable candidate) {
        LauncherPartitionEntry candidateApp;
        LauncherPartitionEntry candidateSpiffs;
        LauncherPartitionEntry candidateFatSys;
        LauncherPartitionEntry candidateFatVfs;
        bool candidateHasSpiffs = false;
        bool candidateHasFatSys = false;
        bool candidateHasFatVfs = false;

        if (!launcherPartitionCreateOtaApp(candidate, appSize, defaultLabel.c_str(), &candidateApp, &error)) {
            return;
        }
        if (!prepareSdDataPartitions(
                candidate,
                spiffs,
                spiffsSize,
                candidateSpiffs,
                candidateHasSpiffs,
                fat,
                fatSizeSys,
                fatLabelSys,
                fatSizeVfs,
                fatLabelVfs,
                candidateFatSys,
                candidateHasFatSys,
                candidateFatVfs,
                candidateHasFatVfs,
                error
            ) ||
            !launcherPartitionValidate(candidate, &error)) {
            return;
        }

        addChoice(
            label,
            candidate,
            candidateApp,
            candidateSpiffs,
            candidateHasSpiffs,
            candidateFatSys,
            candidateHasFatSys,
            candidateFatVfs,
            candidateHasFatVfs
        );
    };

    for (const LauncherPartitionEntry &entry : original.entries) {
        if (!isReplaceableSdApp(entry) || entry.size < requiredAppPartitionSize) continue;
        LauncherPartitionTable candidate = original;
        if (!renameSdEntryByOffset(candidate, entry.offset, defaultLabel)) continue;
        LauncherPartitionEntry candidateApp = entry;
        memset(candidateApp.label, 0, sizeof(candidateApp.label));
        strncpy(candidateApp.label, defaultLabel.c_str(), sizeof(candidateApp.label) - 1);
        LauncherPartitionEntry candidateSpiffs;
        LauncherPartitionEntry candidateFatSys;
        LauncherPartitionEntry candidateFatVfs;
        bool candidateHasSpiffs = false;
        bool candidateHasFatSys = false;
        bool candidateHasFatVfs = false;
        if (prepareSdDataPartitions(
                candidate,
                spiffs,
                spiffsSize,
                candidateSpiffs,
                candidateHasSpiffs,
                fat,
                fatSizeSys,
                fatLabelSys,
                fatSizeVfs,
                fatLabelVfs,
                candidateFatSys,
                candidateHasFatSys,
                candidateFatVfs,
                candidateHasFatVfs,
                error
            ) &&
            launcherPartitionValidate(candidate, &error)) {
            addChoice(
                String("Use ") + entry.label + " partition",
                candidate,
                candidateApp,
                candidateSpiffs,
                candidateHasSpiffs,
                candidateFatSys,
                candidateHasFatSys,
                candidateFatVfs,
                candidateHasFatVfs
            );
        }
    }

    std::vector<LauncherPartitionEntry> apps;
    for (const LauncherPartitionEntry &entry : original.entries) {
        if (isReplaceableSdApp(entry)) apps.push_back(entry);
    }
    std::sort(apps.begin(), apps.end(), [](const LauncherPartitionEntry &a, const LauncherPartitionEntry &b) {
        return a.offset < b.offset;
    });

    for (size_t start = 0; start < apps.size(); ++start) {
        if (apps[start].size >= requiredAppPartitionSize) continue;
        LauncherPartitionTable candidate = original;
        removeSdEntryByOffset(candidate, apps[start].offset);

        LauncherPartitionEntry candidateApp;
        if (!addManualSdAppEntry(
                candidate,
                apps[start].subtype,
                defaultLabel.c_str(),
                apps[start].offset,
                appSize,
                candidateApp,
                error
            )) {
            continue;
        }

        LauncherPartitionEntry candidateSpiffs;
        LauncherPartitionEntry candidateFatSys;
        LauncherPartitionEntry candidateFatVfs;
        bool candidateHasSpiffs = false;
        bool candidateHasFatSys = false;
        bool candidateHasFatVfs = false;
        if (prepareSdDataPartitions(
                candidate,
                spiffs,
                spiffsSize,
                candidateSpiffs,
                candidateHasSpiffs,
                fat,
                fatSizeSys,
                fatLabelSys,
                fatSizeVfs,
                fatLabelVfs,
                candidateFatSys,
                candidateHasFatSys,
                candidateFatVfs,
                candidateHasFatVfs,
                error
            ) &&
            launcherPartitionValidate(candidate, &error)) {
            addChoice(
                String("Repartition ") + apps[start].label + " + free",
                candidate,
                candidateApp,
                candidateSpiffs,
                candidateHasSpiffs,
                candidateFatSys,
                candidateHasFatSys,
                candidateFatVfs,
                candidateHasFatVfs
            );
        }
    }

    if (std::any_of(original.entries.begin(), original.entries.end(), isRemovableSdInstallData)) {
        for (int removalPass = 0; removalPass < 2 && choices.size() == 1; ++removalPass) {
            const bool removeSpiffs = removalPass == 1;
            LauncherPartitionTable candidate = original;
            if (!removeSdInstallDataPartitions(candidate, removeSpiffs)) continue;

            LauncherPartitionEntry candidateApp;
            if (!launcherPartitionCreateOtaApp(
                    candidate, appSize, defaultLabel.c_str(), &candidateApp, &error
                )) {
                continue;
            }

            LauncherPartitionEntry candidateSpiffs;
            LauncherPartitionEntry candidateFatSys;
            LauncherPartitionEntry candidateFatVfs;
            bool candidateHasSpiffs = false;
            bool candidateHasFatSys = false;
            bool candidateHasFatVfs = false;
            if (!prepareSdDataPartitions(
                    candidate,
                    spiffs,
                    spiffsSize,
                    candidateSpiffs,
                    candidateHasSpiffs,
                    fat,
                    fatSizeSys,
                    fatLabelSys,
                    fatSizeVfs,
                    fatLabelVfs,
                    candidateFatSys,
                    candidateHasFatSys,
                    candidateFatVfs,
                    candidateHasFatVfs,
                    error
                ) ||
                !launcherPartitionValidate(candidate, &error)) {
                continue;
            }

            addChoice(
                removeSpiffs ? "Remove data + use free" : "Remove FAT data + use free",
                candidate,
                candidateApp,
                candidateSpiffs,
                candidateHasSpiffs,
                candidateFatSys,
                candidateHasFatSys,
                candidateFatVfs,
                candidateHasFatVfs
            );
        }
    }

    if (std::any_of(original.entries.begin(), original.entries.end(), isRemovableSdInstallData)) {
        for (size_t start = 0; start < apps.size(); ++start) {
            uint32_t rangeEnd = apps[start].offset + apps[start].size;
            for (size_t end = start; end < apps.size(); ++end) {
                if (end > start && apps[end].offset != rangeEnd) break;
                rangeEnd = apps[end].offset + apps[end].size;

                for (int removalPass = 0; removalPass < 2; ++removalPass) {
                    const bool removeSpiffs = removalPass == 1;
                    LauncherPartitionTable candidate = original;
                    for (size_t i = start; i <= end; ++i) removeSdEntryByOffset(candidate, apps[i].offset);
                    if (!removeSdInstallDataPartitions(candidate, removeSpiffs)) continue;

                    String label = String("Remove ") + apps[start].label;
                    if (end > start) label += String("-") + apps[end].label;
                    label += removeSpiffs ? " + all data" : " + FAT data";
                    addAutoLayoutChoice(label, candidate);
                }
            }
        }
    }

    for (size_t start = 0; start < apps.size(); ++start) {
        uint32_t rangeStart = apps[start].offset;
        uint32_t rangeEnd = apps[start].offset + apps[start].size;
        for (size_t end = start; end < apps.size(); ++end) {
            if (end > start && apps[end].offset != rangeEnd) break;
            if (end == start) continue;
            rangeEnd = apps[end].offset + apps[end].size;
            uint32_t usableStart = alignSdUp(rangeStart, LAUNCHER_APP_PARTITION_ALIGNMENT);
            if (rangeEnd <= usableStart || rangeEnd - usableStart < requiredAppPartitionSize) continue;

            LauncherPartitionTable candidate = original;
            for (size_t i = start; i <= end; ++i) removeSdEntryByOffset(candidate, apps[i].offset);

            LauncherPartitionEntry candidateApp;
            if (!addManualSdAppEntry(
                    candidate,
                    apps[start].subtype,
                    defaultLabel.c_str(),
                    usableStart,
                    appSize,
                    candidateApp,
                    error
                )) {
                continue;
            }

            LauncherPartitionEntry candidateSpiffs;
            LauncherPartitionEntry candidateFatSys;
            LauncherPartitionEntry candidateFatVfs;
            bool candidateHasSpiffs = false;
            bool candidateHasFatSys = false;
            bool candidateHasFatVfs = false;
            if (!prepareSdDataPartitions(
                    candidate,
                    spiffs,
                    spiffsSize,
                    candidateSpiffs,
                    candidateHasSpiffs,
                    fat,
                    fatSizeSys,
                    fatLabelSys,
                    fatSizeVfs,
                    fatLabelVfs,
                    candidateFatSys,
                    candidateHasFatSys,
                    candidateFatVfs,
                    candidateHasFatVfs,
                    error
                ) ||
                !launcherPartitionValidate(candidate, &error)) {
                continue;
            }

            String label = String("Repartition ") + apps[start].label + "-" + apps[end].label;
            addChoice(
                label,
                candidate,
                candidateApp,
                candidateSpiffs,
                candidateHasSpiffs,
                candidateFatSys,
                candidateHasFatSys,
                candidateFatVfs,
                candidateHasFatVfs
            );
        }
    }

    const bool needsDataRemoval = choices.size() == 1;
    if (needsDataRemoval &&
        std::any_of(original.entries.begin(), original.entries.end(), isRemovableSdInstallData)) {
        for (int removalPass = 0; removalPass < 2 && choices.size() == 1; ++removalPass) {
            const bool removeSpiffs = removalPass == 1;
            for (size_t start = 0; start < apps.size(); ++start) {
                uint32_t rangeStart = apps[start].offset;
                uint32_t rangeEnd = apps[start].offset + apps[start].size;
                for (size_t end = start; end < apps.size(); ++end) {
                    if (end > start && apps[end].offset != rangeEnd) break;
                    rangeEnd = apps[end].offset + apps[end].size;

                    LauncherPartitionTable candidate = original;
                    for (size_t i = start; i <= end; ++i) removeSdEntryByOffset(candidate, apps[i].offset);
                    if (!removeSdInstallDataPartitions(candidate, removeSpiffs)) continue;

                    LauncherPartitionEntry candidateApp;
                    const uint32_t usableStart = alignSdUp(rangeStart, LAUNCHER_APP_PARTITION_ALIGNMENT);
                    if (rangeEnd <= usableStart || rangeEnd - usableStart < requiredAppPartitionSize)
                        continue;
                    if (!addManualSdAppEntry(
                            candidate,
                            apps[start].subtype,
                            defaultLabel.c_str(),
                            usableStart,
                            appSize,
                            candidateApp,
                            error
                        )) {
                        continue;
                    }

                    LauncherPartitionEntry candidateSpiffs;
                    LauncherPartitionEntry candidateFatSys;
                    LauncherPartitionEntry candidateFatVfs;
                    bool candidateHasSpiffs = false;
                    bool candidateHasFatSys = false;
                    bool candidateHasFatVfs = false;
                    if (!prepareSdDataPartitions(
                            candidate,
                            spiffs,
                            spiffsSize,
                            candidateSpiffs,
                            candidateHasSpiffs,
                            fat,
                            fatSizeSys,
                            fatLabelSys,
                            fatSizeVfs,
                            fatLabelVfs,
                            candidateFatSys,
                            candidateHasFatSys,
                            candidateFatVfs,
                            candidateHasFatVfs,
                            error
                        ) ||
                        !launcherPartitionValidate(candidate, &error)) {
                        continue;
                    }

                    String label = String("Remove ") + apps[start].label;
                    if (end > start) label += String("-") + apps[end].label;
                    label += removeSpiffs ? " + free + all data" : " + free + FAT data";
                    addChoice(
                        label,
                        candidate,
                        candidateApp,
                        candidateSpiffs,
                        candidateHasSpiffs,
                        candidateFatSys,
                        candidateHasFatSys,
                        candidateFatVfs,
                        candidateHasFatVfs
                    );
                }
            }
        }
    }

    choices.push_back({"Cancel", []() {}});
    int selected = 0;
    while (selected == 0) selected = loopOptions(choices);
    if (selected == static_cast<int>(choices.size()) - 1 || selected < 0) {
        error = "Canceled";
        return false;
    }

    if (appEntry.offset == 0) {
        error = "Selected install target failed";
        return false;
    }
    return true;
}

static bool flashRawFromSd(
    File &file, uint32_t sourceOffset, size_t imageSize, const LauncherPartitionEntry &target, bool appImage
) {
    if (!file.seek(sourceOffset)) return false;
    progressHandler(0, imageSize);
    if (!launcherRawUpdateBegin(target.offset, target.size, imageSize, appImage)) return false;

    constexpr size_t bufferSize = 4096;
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[bufferSize]);
    if (!buf) {
        launcherRawUpdateEnd();
        return false;
    }

    size_t written = 0;
    while (written < imageSize) {
        size_t toRead = min(bufferSize, imageSize - written);
        int bytesRead = file.readBytes(reinterpret_cast<char *>(buf.get()), toRead);
        if (bytesRead <= 0) {
            launcherRawUpdateEnd();
            return false;
        }
        if (launcherRawUpdateWrite(buf.get(), bytesRead) != static_cast<size_t>(bytesRead)) return false;
        written += bytesRead;
        progressHandler(written, imageSize);
        launcherDelayMs(1);
    }
    return launcherRawUpdateEnd();
}

static bool readSdBytes(File &file, uint32_t offset, void *buffer, size_t len) {
    if (!file.seek(offset)) return false;
    return file.readBytes(reinterpret_cast<char *>(buffer), len) == len;
}

static uint32_t readLe32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

static String readPartitionLabel(const uint8_t *entry) {
    char label[17] = {0};
    memcpy(label, entry + 12, 16);
    label[16] = '\0';
    return String(label);
}

static uint32_t
boundedSdPartitionPayload(File &file, uint32_t offset, uint32_t declaredSize, uint32_t maxSize) {
    if (offset == 0 || file.size() <= offset || declaredSize == 0) return 0;
    uint32_t availableSize = file.size() - offset;
    return launcherPartitionBoundedPayloadSize(declaredSize, 0, maxSize, availableSize);
}

static bool measureSdEspImage(File &file, uint32_t imageOffset, uint32_t &imageSize) {
    esp_image_header_t header;
    if (!readSdBytes(file, imageOffset, &header, sizeof(header))) return false;
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.segment_count == 0 ||
        header.segment_count > ESP_IMAGE_MAX_SEGMENTS) {
        return false;
    }

    uint32_t cursor = imageOffset + sizeof(header);
    const uint32_t fileSize = file.size();
    if (cursor > fileSize) return false;

    for (uint8_t i = 0; i < header.segment_count; ++i) {
        uint8_t segmentHeader[sizeof(esp_image_segment_header_t)];
        if (!readSdBytes(file, cursor, segmentHeader, sizeof(segmentHeader))) return false;
        const uint32_t segmentSize = readLe32(segmentHeader + 4);
        cursor += sizeof(segmentHeader);
        if (segmentSize > fileSize || cursor > fileSize - segmentSize) return false;
        cursor += segmentSize;
    }

    uint32_t end = alignSdUp(cursor, 16) + 1;
    if (header.hash_appended) end += ESP_IMAGE_HASH_LEN;
    end = alignSdUp(end, 16);
    if (end <= imageOffset || end > fileSize) return false;

    imageSize = end - imageOffset;
    return true;
}

static uint32_t effectiveSdAppSize(File &file, uint32_t appOffset, uint32_t fallbackSize) {
    uint32_t measuredSize = 0;
    if (measureSdEspImage(file, appOffset, measuredSize)) {
        if (fallbackSize == 0 || measuredSize < fallbackSize) {
            launcherConsolePrintf(
                "Measured SD app image at 0x%06X: 0x%06X (%u bytes), fallback was 0x%06X\n",
                appOffset,
                measuredSize,
                measuredSize,
                fallbackSize
            );
            return measuredSize;
        }
    }
    return fallbackSize;
}

static bool installFromSdDynamic(
    File &file, const String &path, uint32_t appSize, uint32_t appOffset, bool spiffs, uint32_t spiffsOffset,
    uint32_t spiffsSize, uint32_t spiffsCopySize, bool fat, uint32_t fatOffsetSys, uint32_t fatSizeSys,
    uint32_t fatCopySizeSys, const String &fatLabelSys, uint32_t fatOffsetVfs, uint32_t fatSizeVfs,
    uint32_t fatCopySizeVfs, const String &fatLabelVfs
) {
    String error;
    LauncherPartitionTable table;
    if (!launcherPartitionReadCurrent(table, &error)) {
        displayRedStripe(error.length() ? error : "Partition read failed");
        launcherDelayMs(2000);
        return false;
    }

    if (appSize == 0 || appOffset + appSize > file.size()) {
        displayRedStripe("Invalid app image");
        launcherDelayMs(2000);
        return false;
    }

    String appLabel = nextSdAppLabel(table, installedAppNameFromPath(path));
    LauncherPartitionEntry appEntry;
    LauncherPartitionEntry spiffsEntry;
    bool hasSpiffsEntry = false;
    LauncherPartitionEntry fatSysEntry;
    LauncherPartitionEntry fatVfsEntry;
    bool hasFatSys = false;
    bool hasFatVfs = false;

    if (!selectSdInstallLayout(
            table,
            appSize,
            appLabel,
            spiffs,
            spiffsSize,
            fat,
            fatSizeSys,
            fatLabelSys.c_str(),
            fatSizeVfs,
            fatLabelVfs.c_str(),
            appEntry,
            spiffsEntry,
            hasSpiffsEntry,
            fatSysEntry,
            hasFatSys,
            fatVfsEntry,
            hasFatVfs,
            error
        )) {
        launcherConsolePrintf("SD install layout failed: %s\n", error.c_str());
        displayRedStripe(error.length() ? error : "No install space");
        launcherDelayMs(2000);
        return false;
    }

    if (!launcherPartitionValidate(table, &error)) {
        displayRedStripe(error.length() ? error : "Invalid table");
        launcherDelayMs(2000);
        return false;
    }

    vTaskSuspend(xHandle);
    bool success = false;
    displayRedStripe("Installing APP");
    prog_handler = 0;
    if (!flashRawFromSd(file, appOffset, appSize, appEntry, true)) {
        displayRedStripe(String("APP: ") + launcherUpdateLastErrorName());
        launcherDelayMs(2000);
        goto DONE;
    }

    if (hasSpiffsEntry && spiffsCopySize > 0) {
        const uint32_t copySize = spiffsCopySize > spiffsEntry.size ? spiffsEntry.size : spiffsCopySize;
        displayRedStripe("Installing SPIFFS");
        prog_handler = 1;
        if (!flashRawFromSd(file, spiffsOffset, copySize, spiffsEntry, false)) {
            displayRedStripe(String("SPIFFS: ") + launcherUpdateLastErrorName());
            launcherDelayMs(2000);
            goto DONE;
        }
    }

    if (hasFatSys && fatCopySizeSys > 0) {
        displayRedStripe("Installing sys FAT");
        prog_handler = 1;
        if (!flashRawFromSd(file, fatOffsetSys, fatCopySizeSys, fatSysEntry, false)) {
            displayRedStripe(String("FAT: ") + launcherUpdateLastErrorName());
            launcherDelayMs(2000);
            goto DONE;
        }
    }
    if (hasFatVfs && fatCopySizeVfs > 0) {
        displayRedStripe("Installing vfs FAT");
        prog_handler = 1;
        if (!flashRawFromSd(file, fatOffsetVfs, fatCopySizeVfs, fatVfsEntry, false)) {
            displayRedStripe(String("FAT: ") + launcherUpdateLastErrorName());
            launcherDelayMs(2000);
            goto DONE;
        }
    }

    displayRedStripe("Writing table");
    if (!launcherPartitionWriteGeneratedTable(table, &error)) {
        displayRedStripe(error.length() ? error : "Table failed");
        launcherDelayMs(2000);
        goto DONE;
    }

    displayRedStripe("Setting boot");
    if (!launcherPartitionSetOtaBoot(table, appEntry.subtype, &error)) {
        displayRedStripe(error.length() ? error : "Boot failed");
        launcherDelayMs(2000);
        goto DONE;
    }

    {
        String installedLabel = String(appEntry.label);
        for (const LauncherAppMetadata &registeredApp : launcherLoadAppRegistry()) {
            if (!launcherPartitionFindByLabel(table, registeredApp.label.c_str())) {
                launcherRemoveAppMetadata(registeredApp.label.c_str());
            }
        }

        LauncherAppMetadata metadata;
        metadata.name = installedAppNameFromPath(path);
        if (metadata.name.isEmpty()) metadata.name = installedLabel;
        metadata.label = installedLabel;
        if (hasFatSys) metadata.fatLabels.push_back(String(fatSysEntry.label));
        if (hasFatVfs) metadata.fatLabels.push_back(String(fatVfsEntry.label));
        launcherSaveAppMetadata(metadata);
        lastInstalledApp = metadata.name;
        saveIntoNVS();
    }

    success = true;

DONE:
    vTaskResume(xHandle);
    return success;
}

/***************************************************************************************
** Function name: updateFromSD
** Description:   this function analyse the .bin and calls performUpdate
***************************************************************************************/
void updateFromSD(String path) {
    uint8_t firstThreeBytes[16];
    uint32_t spiffs_offset = 0;
    uint32_t spiffs_size = 0;
    uint32_t spiffs_copy_size = 0;
    uint32_t app_size = 0;
    uint32_t app_offset = 0;
    bool spiffs = false;
    uint32_t fat_offset_sys = 0;
    uint32_t fat_size_sys = 0;
    uint32_t fat_copy_size_sys = 0;
    String fat_label_sys = "sys";
    uint32_t fat_offset_vfs = 0;
    uint32_t fat_size_vfs = 0;
    uint32_t fat_copy_size_vfs = 0;
    String fat_label_vfs = "vfs";
    bool fat = false;

    File file = SDM.open(path);

    if (!file) goto Exit;
    if (!file.seek(0x8000)) goto Exit;
    file.read(firstThreeBytes, 16);

    if (firstThreeBytes[0] != 0xAA || firstThreeBytes[1] != 0x50 || firstThreeBytes[2] != 0x01) {
        app_size = effectiveSdAppSize(file, 0, file.size());
        if (!installFromSdDynamic(
                file, path, app_size, 0, false, 0, 0, 0, false, 0, 0, 0, "sys", 0, 0, 0, "vfs"
            )) {
            goto Exit;
        }
        file.close();
        tft->fillScreen(BGCOLOR);
        FREE_TFT
        reboot();
    } else {
        if (!file.seek(0x8000)) goto Exit;
        for (int i = 0; i < 0x0A0; i += 0x20) {
            if (!file.seek(0x8000 + i)) goto Exit;
            file.read(firstThreeBytes, 16);

            if (firstThreeBytes[0x02] == 0x00 &&
                (firstThreeBytes[0x03] == 0x00 || firstThreeBytes[0x03] == 0x10 ||
                 firstThreeBytes[0x03] == 0x20) &&
                app_size == 0) {
                uint32_t declared_app_size = readLe32(firstThreeBytes + 0x08);
                app_offset = readLe32(firstThreeBytes + 0x04);
                if (file.size() < (declared_app_size + app_offset)) {
                    app_size = file.size() - app_offset;
                    launcherConsolePrintf(
                        "Using SD app tail size at 0x%06X: 0x%06X (%u bytes), declared partition was "
                        "0x%06X\n",
                        app_offset,
                        app_size,
                        app_size,
                        declared_app_size
                    );
                } else {
                    app_size = declared_app_size;
                    app_size = effectiveSdAppSize(file, app_offset, app_size);
                }
            }

            if (firstThreeBytes[0x02] == 0x01 && firstThreeBytes[3] == 0x82) {
                spiffs_offset = readLe32(firstThreeBytes + 0x04);
                const uint32_t declaredSpiffsSize = readLe32(firstThreeBytes + 0x08);
                spiffs_size = declaredSpiffsSize > sdLargeSpiffsThreshold() ? sdUseRemainingSpiffsSize()
                                                                            : sdDefaultSpiffsSize();
                spiffs_copy_size = boundedSdPartitionPayload(
                    file,
                    spiffs_offset,
                    declaredSpiffsSize,
                    spiffs_size == sdUseRemainingSpiffsSize() ? declaredSpiffsSize : spiffs_size
                );
                spiffs = true;
                if (file.size() < spiffs_offset) {
                    spiffs_copy_size = 0;
                    launcherConsolePrintf(
                        "Found SPIFFS table entry without payload: create 0x%06X, copy 0\n", spiffs_size
                    );
                }
            }

            if (firstThreeBytes[0x02] == 0x01 && firstThreeBytes[3] == 0x81) {
                String label = readPartitionLabel(firstThreeBytes);
                uint32_t offset = readLe32(firstThreeBytes + 0x04);
                uint32_t declaredSize = readLe32(firstThreeBytes + 0x08);
                uint32_t availableSize = offset != 0 && file.size() > offset ? file.size() - offset : 0;
                LauncherPartitionPayloadPlan payload =
                    launcherPartitionFatPayloadPlan(label.c_str(), declaredSize, 0, availableSize);
                fat = true;
                if (label == "sys" || label == "ffat") {
                    fat_label_sys = label;
                    fat_offset_sys = offset;
                    fat_size_sys = payload.partitionSize;
                    fat_copy_size_sys = payload.copySize;
                } else if (fat_size_vfs == 0 || label == "vfs" || label == "vsf") {
                    fat_label_vfs = label;
                    fat_offset_vfs = offset;
                    fat_size_vfs = payload.partitionSize;
                    fat_copy_size_vfs = payload.copySize;
                }
                launcherConsolePrintf(
                    "Found FAT %s at 0x%06X: create 0x%06X, copy 0x%06X of declared 0x%06X\n",
                    label.c_str(),
                    offset,
                    payload.partitionSize,
                    payload.copySize,
                    declaredSize
                );
            }
        }

        // log_i("Appsize: %d", app_size);
        // log_i("Spiffsize: %d", spiffs_size);
        // log_i("FATsize[0]: %d - max: %d at offset: %d", fat_size_sys, MAX_FAT_sys, fat_offset_sys);
        // log_i("FATsize[1]: %d - max: %d at offset: %d", fat_size_vfs, MAX_FAT_vfs, fat_offset_vfs);
        // log_i("FAT: %d", fat);
        // log_i("------------------------");

        if (!fat) {
            fat_size_sys = 0;
            fat_size_vfs = 0;
            fat_copy_size_sys = 0;
            fat_copy_size_vfs = 0;
            fat_offset_sys = 0;
            fat_offset_vfs = 0;
        }

        prog_handler = 0; // Install flash update
        if (askSpiffs == false) spiffs_copy_size = 0;
        if (spiffs && askSpiffs && spiffs_copy_size > 0) {
            bool copySpiffs = true;
            options = {
                {"SPIFFS No",  [&]() { copySpiffs = false; } },
                {"SPIFFS Yes", [&]() { copySpiffs = true; }  },
                {"Cancel",     [&]() { returnToMenu = true; }}
            };
            if (loopOptions(options) < 0 || returnToMenu) {
                file.close();
                tft->fillScreen(BGCOLOR);
                return;
            }
            if (!copySpiffs) spiffs_copy_size = 0;
            tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
        }

        log_i("Appsize: %d", app_size);
        log_i("Spiffsize: %d", spiffs_size);
        log_i("FATsize[0]: %d - max: %d at offset: %d", fat_size_sys, MAX_FAT_sys, fat_offset_sys);
        log_i("FATsize[1]: %d - max: %d at offset: %d", fat_size_vfs, MAX_FAT_vfs, fat_offset_vfs);

        if (!installFromSdDynamic(
                file,
                path,
                app_size,
                app_offset,
                spiffs,
                spiffs_offset,
                spiffs_size,
                spiffs_copy_size,
                fat,
                fat_offset_sys,
                fat_size_sys,
                fat_copy_size_sys,
                fat_label_sys,
                fat_offset_vfs,
                fat_size_vfs,
                fat_copy_size_vfs,
                fat_label_vfs
            )) {
            goto Exit;
        }
        displayRedStripe("Complete");
        launcherDelayMs(1000);
        FREE_TFT
        reboot();
    }
Exit:
    displayRedStripe("Update Error.");
    launcherDelayMs(2500);
}

/***************************************************************************************
** Function name: performFATUpdate
** Description:   this function performs the update
***************************************************************************************/
uint8_t buffer2[1024];

bool performFATUpdate(Stream &updateSource, size_t updateSize, const char *label) {
    // Preencher o buffer com 0xFF
    memset(buffer2, 0x00, sizeof(buffer2));
    const esp_partition_t *partition;
    esp_err_t error;
    size_t paroffset = 0;
    int written = 0;
    int bytesRead = 0;
    error = esp_flash_set_chip_write_protect(NULL, false);

    if (error != ESP_OK) {
        log_i("Protection error: %d", error);
        // return false;
    }

    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, label);
    if (!partition) { return false; }

    log_i("Start updating: %s", partition->label);
    paroffset = partition->address;
    log_i("Erasing updating: %s from: %d with size: %d", label, paroffset, updateSize);

    error = esp_flash_erase_region(NULL, partition->address, updateSize);
    if (error != ESP_OK) {
        log_i("Erase error %d", error);
        return false;
    }

    progressHandler(0, 500);
    displayRedStripe("Updating FAT");
    log_i("Updating updating: %s", label);

    while (written < updateSize) { // updateSource.available() &&
        bytesRead = updateSource.readBytes(buffer2, sizeof(buffer2));
        error = esp_flash_write(NULL, buffer2, paroffset, bytesRead);
        if (error != ESP_OK) {
            log_i("[FLASH] Failed to write to flash (0x%x)", error);
            return false;
        }
        if (bytesRead == 0) break; // Evitar loop infinito se não houver bytes para ler
        paroffset += bytesRead;
        written += bytesRead;
        progressHandler(written, updateSize);
    }

    if (written == updateSize) {
        log_i("Success updating %s", label);
    } else {
        log_i("FAIL updating %s", label);
        return false;
    }

    return true;
}
