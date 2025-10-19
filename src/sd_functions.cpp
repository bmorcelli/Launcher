#include "sd_functions.h"
#include "display.h"
#include "esp_log.h"
#include "mykeyboard.h"
#include <algorithm> // for std::sort
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <globals.h>
SPIClass sdcardSPI;
String fileToCopy;
String fileToUse;

#ifndef PART_04MB
/***************************************************************************************
** Function name: eraseFAT
** Description:   erase FAT partition to micropython compatibilities
***************************************************************************************/
bool eraseFAT() {
    esp_err_t err;

    // Find FAT partition with its name
    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "vfs");
    if (!partition) {
        // log_e("Failed to find partition");
        return false;
    }

    // erase all FAT partition
    err = esp_flash_erase_region(NULL, partition->address, partition->size);
    if (err != ESP_OK) {
        // log_e("Failed to erase partition: %s", esp_err_to_name(err));
        return false;
    }

    // Find FAT partition with its name
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "sys");
    if (!partition) {
        // log_e("Failed to find partition");
        goto Exit;
    }

    // erase all FAT partition
    err = esp_flash_erase_region(NULL, partition->address, partition->size);
    if (err != ESP_OK) { return false; }
Exit:
    return true;
}
#endif
/***************************************************************************************
** Function name: setupSdCard
** Description:   Start SD Card
***************************************************************************************/
bool setupSdCard() {
#if !defined(SDM_SD) // fot Lilygo T-Display S3 with lilygo shield
    if (!SD_MMC.begin("/sdcard", true))
#elif (TFT_MOSI == SDCARD_MOSI)
    if (!SDM.begin(SDCARD_CS)) // https://github.com/Bodmer/TFT_eSPI/discussions/2420
#elif defined(HEADLESS)
    if (_sck == 0 && _miso == 0 && _mosi == 0 && _cs == 0) {
        Serial.println("SdCard pins not set");
        return false;
    }

    sdcardSPI.begin(_sck, _miso, _mosi, _cs); // start SPI communications
    vTaskDelay(pdTICKS_TO_MS(10));
    if (!SDM.begin(_cs, sdcardSPI))
#elif defined(DONT_USE_INPUT_TASK)
#if (TFT_MOSI != SDCARD_MOSI)
    sdcardSPI.begin(SDCARD_SCK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS); // start SPI communications
    if (!SDM.begin(SDCARD_CS, sdcardSPI))
#else
    if (!SDM.begin(SDCARD_CS))
#endif

#else
    sdcardSPI.begin(SDCARD_SCK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS); // start SPI communications
    vTaskDelay(pdTICKS_TO_MS(10));
    if (!SDM.begin(SDCARD_CS, sdcardSPI))
#endif
    {
        // sdcardSPI.end(); // Closes SPI connections and release pin header.
        Serial.println("Failed to mount SDCARD");
        sdcardMounted = false;
        return false;
    } else {
        Serial.println("SDCARD mounted successfully");
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
        return false;
    }
    return true;
}

/***************************************************************************************
** Function name: sortList
** Description:   sort files/folders by name
***************************************************************************************/
bool sortList(const Option &a, const Option &b) {
    if (a.color != b.color) {
        return a.color > b.color; // true if a is a folder and b is not
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
        return; // Retornar imediatamente se não for possível abrir o diretório
    }

    while (true) {
        bool isDir;
        String fullPath = root.getNextFileName(&isDir);
        String nameOnly = fullPath.substring(fullPath.lastIndexOf("/") + 1);
        if (fullPath == "") { break; }
        // Serial.printf("Path: %s (isDir: %d)\n", fullPath.c_str(), isDir);

        uint16_t color = FGCOLOR - 0x1111;

        if (!isDir) {
            int dotIndex = nameOnly.lastIndexOf(".");
            String ext = dotIndex >= 0 ? nameOnly.substring(dotIndex + 1) : "";
            ext.toUpperCase();
            if (onlyBins && !ext.equals("BIN")) { continue; }
            color = FGCOLOR;
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
    LongPressTmp = millis();
    while (millis() - LongPressTmp < 300 && SelPress) {
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
            Serial.printf("Going : Folder    = %s\nPreFolder = %s\n", Folder, PreFolder);
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
            Serial.printf("Backing: Folder    = %s\nPreFolder = %s\n", Folder, PreFolder);
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
void performUpdate(Stream &updateSource, size_t updateSize, int command) {
    // command = U_FAT_vfs = 300
    // command = U_FAT_sys = 400
    // command = U_SPIFFS = 100
    // command = U_FLASH = 0

    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
    progressHandler(0, 500);

    vTaskSuspend(xHandle);
    if (Update.begin(updateSize, command)) {
        int written = 0;
        uint8_t buf[1024];
        int bytesRead;

        prog_handler = 0; // Install flash update
        if (command == U_SPIFFS || command == U_FAT_vfs || command == U_FAT_sys)
            prog_handler = 1; // Install flash update
        log_i("updateSize = %d", updateSize);
        while (written < updateSize) { // updateSource.available() > 0 &&
            bytesRead = updateSource.readBytes(buf, sizeof(buf));
            written += Update.write(buf, bytesRead);
            progressHandler(written, updateSize);
        }
        if (Update.end()) {
            if (Update.isFinished()) log_i("Update successfully completed.");
            else log_i("Update not finished? Something went wrong!");
        } else {
            log_i("Error Occurred. Error #: %s", String(Update.getError()));
        }
    } else {
        uint8_t error = Update.getError();
        displayRedStripe("E:" + String(error) + "-Wrong Partition Scheme");
        delay(2500);
    }
    vTaskResume(xHandle);
}

/***************************************************************************************
** Function name: updateFromSD
** Description:   this function analyse the .bin and calls performUpdate
***************************************************************************************/
void updateFromSD(String path) {
    uint8_t firstThreeBytes[16];
    uint32_t spiffs_offset = 0;
    uint32_t spiffs_size = 0;
    uint32_t app_size = 0;
    bool spiffs = false;
    uint32_t fat_offset_sys = 0;
    uint32_t fat_size_sys = 0;
    uint32_t fat_offset_vfs = 0;
    uint32_t fat_size_vfs = 0;
    bool fat = false;

    File file = SDM.open(path);

    if (!file) goto Exit;
    if (!file.seek(0x8000)) goto Exit;
    file.read(firstThreeBytes, 16);

    if (firstThreeBytes[0] != 0xAA || firstThreeBytes[1] != 0x50 || firstThreeBytes[2] != 0x01) {
        if (!file.seek(0x0)) goto Exit;
        performUpdate(file, file.size(), U_FLASH);
        file.close();
        tft->fillScreen(BGCOLOR);
        FREE_TFT
#if CONFIG_IDF_TARGET_ESP32P4
        const esp_partition_t *partition =
            esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
        esp_ota_set_boot_partition(partition);
        ESP.deepSleep(100);
#endif
        ESP.restart();
    } else {
        if (!file.seek(0x8000)) goto Exit;
        for (int i = 0; i < 0x0A0; i += 0x20) {
            if (!file.seek(0x8000 + i)) goto Exit;
            file.read(firstThreeBytes, 16);

            if ((firstThreeBytes[0x03] == 0x00 || firstThreeBytes[0x03] == 0x10 ||
                 firstThreeBytes[0x03] == 0x20) &&
                firstThreeBytes[0x06] == 0x01) {
                app_size = (firstThreeBytes[0x0A] << 16) | (firstThreeBytes[0x0B] << 8) | 0x00;
                if (file.size() < (app_size + 0x10000)) app_size = file.size() - 0x10000;
                else if (app_size > MAX_APP) app_size = MAX_APP;
            }

            if (firstThreeBytes[3] == 0x82) {
                spiffs_offset =
                    (firstThreeBytes[0x06] << 16) | (firstThreeBytes[0x07] << 8) | firstThreeBytes[0x08];
                spiffs_size = (firstThreeBytes[0x0A] << 16) | (firstThreeBytes[0x0B] << 8) | 0x00;
                if (file.size() < spiffs_offset) spiffs = false;
                else if (spiffs_size > MAX_SPIFFS) {
                    spiffs_size = MAX_SPIFFS;
                    spiffs = true;
                }
                if (spiffs && file.size() < (spiffs_offset + spiffs_size))
                    spiffs_size = file.size() - spiffs_offset;
            }

            if (firstThreeBytes[3] == 0x81 && firstThreeBytes[0x0C] == 0x73) {
                fat_offset_sys =
                    (firstThreeBytes[0x06] << 16) | (firstThreeBytes[0x07] << 8) | firstThreeBytes[0x08];
                fat_size_sys = (firstThreeBytes[0x0A] << 16) | (firstThreeBytes[0x0B] << 8) | 0x00;
                if (file.size() < fat_offset_sys) fat = false;
                else fat = true;
                if (fat && fat_size_sys > MAX_FAT_sys) fat_size_sys = MAX_FAT_sys;
                if (fat && file.size() < (fat_offset_sys + fat_size_sys))
                    fat_size_sys = file.size() - fat_offset_sys;
            }

            if (firstThreeBytes[3] == 0x81 && firstThreeBytes[0x0C] == 0x76) {
                fat_offset_vfs =
                    (firstThreeBytes[0x06] << 16) | (firstThreeBytes[0x07] << 8) | firstThreeBytes[0x08];
                fat_size_vfs = (firstThreeBytes[0x0A] << 16) | (firstThreeBytes[0x0B] << 8) | 0x00;
                if (file.size() < fat_offset_vfs) fat = false;
                else fat = true;
                if (fat && fat_size_vfs > MAX_FAT_vfs) fat_size_vfs = MAX_FAT_vfs;
                if (fat && file.size() < (fat_offset_vfs + fat_size_vfs))
                    fat_size_vfs = file.size() - fat_offset_vfs;
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
            fat_offset_sys = 0;
            fat_offset_vfs = 0;
        }

        prog_handler = 0; // Install flash update
        if (spiffs && askSpiffs) {
            options = {
                {"SPIFFS No",  [&]() { spiffs = false; }     },
                {"SPIFFS Yes", [&]() { spiffs = true; }      },
                {"Cancel",     [&]() { returnToMenu = true; }}
            };
            if (loopOptions(options) < 0 || returnToMenu) {
                file.close();
                tft->fillScreen(BGCOLOR);
                return;
            }
            tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
        }

        log_i("Appsize: %d", app_size);
        log_i("Spiffsize: %d", spiffs_size);
        log_i("FATsize[0]: %d - max: %d at offset: %d", fat_size_sys, MAX_FAT_sys, fat_offset_sys);
        log_i("FATsize[1]: %d - max: %d at offset: %d", fat_size_vfs, MAX_FAT_vfs, fat_offset_vfs);

        if (!file.seek(0x10000)) goto Exit;
        performUpdate(file, app_size, U_FLASH);

        prog_handler = 1; // Install SPIFFS update
        if (spiffs) {
            if (!file.seek(spiffs_offset)) goto Exit;
            performUpdate(file, spiffs_size, U_SPIFFS);
        }

        if (fat) {
            displayRedStripe("Formating FAT");
            if (fat_size_sys > 0) {
                if (!file.seek(fat_offset_sys)) goto Exit;
                if (!performFATUpdate(file, fat_size_sys, "sys")) log_i("FAIL updating FAT sys");
                else displayRedStripe("sys FAT complete");
            }
            displayRedStripe("Formating FAT");
            if (fat_size_vfs > 0) {
                if (!file.seek(fat_offset_vfs)) goto Exit;
                if (!performFATUpdate(file, fat_size_vfs, "vfs")) log_i("FAIL updating FAT vfs");
                else displayRedStripe("vfs FAT complete");
            }
        }
        displayRedStripe("Complete");
        delay(1000);
        FREE_TFT
#if CONFIG_IDF_TARGET_ESP32P4
        const esp_partition_t *partition =
            esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
        esp_ota_set_boot_partition(partition);
        ESP.deepSleep(100);
#endif
        ESP.restart();
    }
Exit:
    displayRedStripe("Update Error.");
    delay(2500);
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
    if (!partition) {
        error = UPDATE_ERROR_NO_PARTITION;
        return false;
    }

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
