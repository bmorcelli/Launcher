#include "sd_functions.h"
#include "app_registry.h"
#include "display.h"
#include "esp_log.h"
#include "idf/idf_update.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "partition_install_layout.h"
#include "partition_table_model.h"
#include "settings.h"
#include <algorithm>
#include <esp_app_format.h>
#include <esp_image_format.h>
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
    String fullPath = dir.getNextFileName(&isDir);
    while (fullPath != "") {
        if (isDir) {
            success &= deleteFromSd(fullPath);
        } else {
            success &= SDM.remove(fullPath.c_str());
        }
        fullPath = dir.getNextFileName(&isDir);
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
    if (launcherUpdateTargetFromCommand(command, target)) {
        prog_handler = target == LAUNCHER_UPDATE_APP ? 0 : 1;
        log_i("updateSize = %d", updateSize);
        if (launcherUpdateStream(updateSource, updateSize, target, progressHandler) &&
            launcherUpdateIsFinished()) {
            log_i("Update successfully completed. Rebooting.");
            displayRedStripe("Post Install Cleanup");
            launcherClearCoredump();
            success = true;
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

    uint32_t end = launcherAlignUp(cursor, 16) + 1;
    if (header.hash_appended) end += ESP_IMAGE_HASH_LEN;
    end = launcherAlignUp(end, 16);
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
    uint32_t spiffsSize, uint32_t spiffsCopySize, std::vector<LauncherInstallFatPartition> &fatPartitions
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

    String appLabel = launcherPartitionNextAppLabel(table, installedAppNameFromPath(path));
    LauncherPartitionEntry appEntry;
    LauncherPartitionEntry spiffsEntry;
    bool hasSpiffsEntry = false;

    if (!launcherSelectInstallLayout(
            table,
            appSize,
            appLabel,
            spiffs,
            spiffsSize,
            fatPartitions,
            appEntry,
            spiffsEntry,
            hasSpiffsEntry,
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

    for (const LauncherInstallFatPartition &fatPartition : fatPartitions) {
        if (!fatPartition.hasEntry || fatPartition.copySize == 0) continue;
        displayRedStripe(String("Installing ") + fatPartition.label);
        prog_handler = 1;
        if (!flashRawFromSd(
                file, fatPartition.sourceOffset, fatPartition.copySize, fatPartition.entry, false
            )) {
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
        for (const LauncherInstallFatPartition &fatPartition : fatPartitions) {
            if (fatPartition.hasEntry) metadata.fatLabels.push_back(String(fatPartition.entry.label));
        }
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
    uint8_t partitionEntry[LAUNCHER_PARTITION_ENTRY_SIZE];
    uint32_t spiffs_offset = 0;
    uint32_t spiffs_size = 0;
    uint32_t spiffs_copy_size = 0;
    uint32_t app_size = 0;
    uint32_t app_offset = 0;
    bool spiffs = false;
    std::vector<LauncherInstallFatPartition> fatPartitions;

    File file = SDM.open(path);

    if (!file) goto Exit;
    if (!file.seek(0x8000)) goto Exit;
    file.read(partitionEntry, 16);

    if (partitionEntry[0] != 0xAA || partitionEntry[1] != 0x50 || partitionEntry[2] != 0x01) {
        app_size = effectiveSdAppSize(file, 0, file.size());
        std::vector<LauncherInstallFatPartition> fatPartitions;
        if (!installFromSdDynamic(file, path, app_size, 0, false, 0, 0, 0, fatPartitions)) { goto Exit; }
        file.close();
        tft->fillScreen(BGCOLOR);
        FREE_TFT
        reboot();
    } else {
        if (!file.seek(0x8000)) goto Exit;
        for (int i = 0; i < LAUNCHER_PARTITION_TABLE_SIZE; i += LAUNCHER_PARTITION_ENTRY_SIZE) {
            if (!file.seek(0x8000 + i)) goto Exit;
            if (file.read(partitionEntry, LAUNCHER_PARTITION_ENTRY_SIZE) != LAUNCHER_PARTITION_ENTRY_SIZE)
                goto Exit;
            if (partitionEntry[0] == 0xEB && partitionEntry[1] == 0xEB) break;
            if (partitionEntry[0] == 0xFF && partitionEntry[1] == 0xFF) break;

            if (partitionEntry[0x02] == 0x00 &&
                (partitionEntry[0x03] == 0x00 || partitionEntry[0x03] == 0x10 ||
                 partitionEntry[0x03] == 0x20) &&
                app_size == 0) {
                uint32_t declared_app_size = readLe32(partitionEntry + 0x08);
                app_offset = readLe32(partitionEntry + 0x04);
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

            if (partitionEntry[0x02] == 0x01 && partitionEntry[3] == 0x82) {
                spiffs_offset = readLe32(partitionEntry + 0x04);
                const uint32_t declaredSpiffsSize = readLe32(partitionEntry + 0x08);
                spiffs_size = declaredSpiffsSize > LAUNCHER_DEFAULT_SPIFFS_THRESHOLD
                                  ? LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE
                                  : LAUNCHER_DEFAULT_SPIFFS_SIZE;
                spiffs_copy_size = boundedSdPartitionPayload(
                    file,
                    spiffs_offset,
                    declaredSpiffsSize,
                    spiffs_size == LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE ? declaredSpiffsSize
                                                                              : spiffs_size
                );
                spiffs = true;
                if (file.size() < spiffs_offset) {
                    spiffs_copy_size = 0;
                    launcherConsolePrintf(
                        "Found SPIFFS table entry without payload: create 0x%06X, copy 0\n", spiffs_size
                    );
                }
            }

            if (partitionEntry[0x02] == 0x01 && partitionEntry[3] == 0x81) {
                LauncherInstallFatPartition fatPartition;
                fatPartition.label = readPartitionLabel(partitionEntry);
                uint32_t declaredSize = readLe32(partitionEntry + 0x08);
                fatPartition.sourceOffset = readLe32(partitionEntry + 0x04);
                if (fatPartition.label.isEmpty()) {
                    fatPartition.label = fatPartitions.empty() ? "sys" : "vfs";
                }
                uint32_t offset = fatPartition.sourceOffset;
                uint32_t availableSize = offset != 0 && file.size() > offset ? file.size() - offset : 0;
                LauncherPartitionPayloadPlan payload = launcherPartitionFatPayloadPlan(
                    fatPartition.label.c_str(), declaredSize, 0, availableSize
                );
                fatPartition.partitionSize = payload.partitionSize;
                fatPartition.copySize = payload.copySize;
                fatPartitions.push_back(fatPartition);
                launcherConsolePrintf(
                    "Found FAT %s at 0x%06X: create 0x%06X, copy 0x%06X of declared 0x%06X\n",
                    fatPartition.label.c_str(),
                    offset,
                    payload.partitionSize,
                    payload.copySize,
                    declaredSize
                );
            }
        }

        // log_i("Appsize: %d", app_size);
        // log_i("Spiffsize: %d", spiffs_size);
        // log_i("FAT count: %d", fatPartitions.size());
        // log_i("------------------------");

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
        log_i("FAT count: %d", fatPartitions.size());

        if (!installFromSdDynamic(
                file,
                path,
                app_size,
                app_offset,
                spiffs,
                spiffs_offset,
                spiffs_size,
                spiffs_copy_size,
                fatPartitions
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
bool performFATUpdate(Stream &updateSource, size_t updateSize, const char *label) {
    progressHandler(0, 500);
    displayRedStripe("Updating FAT");
    log_i("Updating FAT partition: %s", label);

    LauncherUpdateTarget target =
        strcmp(label, "sys") == 0 ? LAUNCHER_UPDATE_FAT_SYS : LAUNCHER_UPDATE_FAT_VFS;
    if (!launcherUpdateStream(updateSource, updateSize, target, progressHandler)) {
        log_i("FAIL updating %s", label);
        return false;
    }

    log_i("Success updating %s", label);
    return true;
}
