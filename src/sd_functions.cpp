#include "sd_functions.h"
#include "backup_manager.h"
#include "display.h"
#include "esp_log.h"
#include "idf/idf_update.h"
#include "idf/launcher_platform.h"
#include "install_shared.h"
#include "littlefs_patch.h"
#include "mykeyboard.h"
#include "partition_install_layout.h"
#include "partition_table_model.h"
#include "ram_profile.h"
#include "settings.h"
#include "utils.h"
#include <algorithm>
#include <esp_app_format.h>
#include <esp_image_format.h>
#include <esp_partition.h>
#include <globals.h>
#include <memory>
SPIClass sdcardSPI;
String fileToCopy;
String fileToUse;

static inline void pauseSdInstallInput() {
    if (xHandle != nullptr) vTaskSuspend(xHandle);
}

static inline void resumeSdInstallInput() {
    if (xHandle != nullptr) vTaskResume(xHandle);
}

bool setupSdCard() {
#if !defined(SDM_SD)
    if (sdcardMounted) return true;
    bool OnebitMode = true; // default to one bit mode
#if defined(USE_SD_MMC) && defined(PIN_SD_CLK) && defined(PIN_SD_CMD) && defined(PIN_SD_D0)
#if defined(SD_MMC_4BIT) && defined(PIN_SD_D1) && defined(PIN_SD_D2) && defined(PIN_SD_D3)
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    OnebitMode = false;
#else
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
#endif
    vTaskDelay(pdTICKS_TO_MS(10));
#else
#endif
    if (!SD_MMC.begin("/sdcard", OnebitMode, false)) // One bit mode, don't auto-format
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
** Function name: deleteFromSd
** Description:   delete file or folder
***************************************************************************************/
bool deleteFromSd(const String &path) {
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
        yield(); // large folders can take long enough to trip the task watchdog
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
bool renameFile(const String &path, const String &filename) {
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
bool copyFile(const String &path) {
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
        displayError("Cannot copy Folder");
        file.close();
        return false;
    }
}

/***************************************************************************************
** Function name: pasteFile
** Description:   paste file to new folder
***************************************************************************************/
bool pasteFile(const String &path) {
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
            float rad = (tot > 0) ? (360.0f * prog / tot) : 0.0f;
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
        displayError("Couldn't create folder");
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
        displayError("SD not found or not formatted in FAT32");
        return; // Retornar imediatamente em caso de falha
    }
    File root = SDM.open(folder);
    if (!root || !root.isDirectory()) {
        displayError("Fail open root");
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
#if defined(HAS_KEYBOARD)
#ifndef RESERVED_NAV_KEYS
#define RESERVED_NAV_KEYS ""
#endif
static bool isReservedBindKey(char c) {
    for (const char *p = RESERVED_NAV_KEYS; *p; ++p) {
        if (*p == c) return true;
    }
    return false;
}

static bool captureBindKey(char &outKey) {
    displayRedStripe("Press a key to bind...");
    while (true) {
        keyStroke key = _getKeyPress();
        if (key.pressed) {
            if (key.exit_key) return false;
            if (!key.enter && !key.word.empty()) {
                char candidate = key.word[0];
                if (candidate >= 'A' && candidate <= 'Z') candidate += ('a' - 'A');
                if (isReservedBindKey(candidate)) {
                    displayRedStripe("Key reserved for navigation");
                    continue;
                }
                outKey = candidate;
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void bindFileToKeyMenu(const String &path) {
    char keyChar = 0;
    if (!captureBindKey(keyChar)) return;
    String key = String(keyChar);
    setKeyBinding(key, path);
    displayMsg(String("'") + key + "' bound");
}
#endif

/*********************************************************************
**  Function: loopSD
**  Where you choose what to do wuth your SD Files
**********************************************************************/
String loopSD(bool filePicker) {
    RAM_LOG(filePicker ? "loopSD-picker-start" : "loopSD-start");
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
        RAM_LOG("loopSD-before-readFs");
        readFs(Folder, options);
        RAM_LOG("loopSD-after-readFs");
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
#if defined(HAS_TOUCH) && defined(DONT_USE_INPUT_TASK) && !defined(E_PAPER_DISPLAY)
    // Touch build with inline input polling (pancake, Marauder V8/V4OG, CYD,
    // NM-CYD-C5, elecrow, nesso...): the button-style "is SelPress still held"
    // test below does not work here. The touch layer emits presses but no
    // reliable "released" event, so a quick tap leaves SelPress asserted and is
    // mis-read as a long press (opening the folder's options menu instead of the
    // folder itself). Instead poll the panel directly: clearing touchPoint.pressed
    // and re-running InputHandler sets it back to true only while a finger is
    // physically on the panel. Finger held past the threshold = long press
    // (options); a quick tap that releases first = short press (open the folder).
    // Boards that drive input from a background task (and thus manage LongPress
    // themselves) are intentionally excluded — they keep the button path below.
    {
        const uint32_t holdThreshold = 400; // ms the finger must stay down
        const uint32_t t0 = launcherMillis();
        LongPress = true; // force InputHandler to poll on every call (skip debounce)
        while (launcherMillis() - t0 < holdThreshold) {
            touchPoint.pressed = false;
            InputHandler();
            if (!touchPoint.pressed) break; // finger lifted → short press
            vTaskDelay(15 / portTICK_PERIOD_MS);
        }
        if (launcherMillis() - t0 >= holdThreshold) LongPressDetected = true;
        // On a long press the finger is usually still down; wait for release so
        // the lingering touch doesn't immediately activate an item in the menu.
        if (LongPressDetected) {
            const uint32_t rel = launcherMillis();
            while (launcherMillis() - rel < 1500) {
                touchPoint.pressed = false;
                InputHandler();
                if (!touchPoint.pressed) break;
                vTaskDelay(15 / portTICK_PERIOD_MS);
            }
        }
        LongPress = false;
        resetGlobals(); // drop any flags / heat-map side effects from polling
    }
#elif !defined(E_PAPER_DISPLAY)
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
#if defined(HAS_KEYBOARD)
        {
            String upperFile = fileToUse;
            upperFile.toUpperCase();
            if (upperFile.endsWith(".BIN")) {
                opt.insert(opt.begin() + 1, {"Bind to key", [=]() { bindFileToKeyMenu(fileToUse); }});
            }
        }
#endif
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

static String installedAppNameFromPath(const String &path) { return launcherInstallAppDisplayName(path); }

static bool flashRawFromSd(
    File &file, uint32_t sourceOffset, size_t imageSize, const LauncherPartitionEntry &target, bool appImage
) {
    if (!file.seek(sourceOffset)) {
        launcherConsolePrintf(
            "flashRawFromSd: initial seek to 0x%08X failed, retrying\n", (unsigned)sourceOffset
        );
        return false;
    }
    progressHandler(0, imageSize);
    if (!launcherRawUpdateBegin(target.offset, target.size, imageSize, appImage)) {
        launcherConsolePrintf(
            "flashRawFromSd: launcherRawUpdateBegin failed target=0x%08X/0x%08X image=0x%08X (%s)\n",
            (unsigned)target.offset,
            (unsigned)target.size,
            (unsigned)imageSize,
            launcherUpdateLastErrorName()
        );
        return false;
    }

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
            launcherDelayMs(20);
            if (!file.seek(sourceOffset + written)) {
                launcherConsolePrintf(
                    "SD read failed at offset 0x%08X (re-seek failed)\n", (unsigned)(sourceOffset + written)
                );
                launcherRawUpdateEnd();
                return false;
            }
            bytesRead = file.readBytes(reinterpret_cast<char *>(buf.get()), toRead);
        }
        if (bytesRead <= 0) {
            launcherConsolePrintf("SD read failed at offset 0x%08X\n", (unsigned)(sourceOffset + written));
            launcherRawUpdateEnd();
            return false;
        }
        if (launcherRawUpdateWrite(buf.get(), bytesRead) != static_cast<size_t>(bytesRead)) {
            launcherConsolePrintf(
                "Flash write failed at partition offset 0x%08X (%s)\n",
                (unsigned)written,
                launcherUpdateLastErrorName()
            );
            launcherRawUpdateEnd();
            return false;
        }
        written += bytesRead;
        progressHandler(written, imageSize);
        launcherDelayMs(1);
    }
    if (!launcherRawUpdateEnd()) {
        launcherConsolePrintf(
            "flashRawFromSd: launcherRawUpdateEnd failed (%s)\n", launcherUpdateLastErrorName()
        );
        return false;
    }

    if (!appImage) {
        String patchError;
        if (!launcherPatchReducedLittlefsSuperblocks(target, &patchError)) {
            launcherConsolePrintf(
                "LittleFS patch failed after SD copy label=%s offset=0x%08X size=0x%08X: %s\n",
                target.label,
                target.offset,
                target.size,
                patchError.c_str()
            );
            return false;
        }
    }

    return true;
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

// A partition table entry can be present without actually carrying payload (e.g. the
// image was built with an empty/erased data partition). Treat it as empty when its
// first bytes are all-0xFF or all-0x00, so callers don't size/copy for data that isn't there.
static bool sdPartitionIsEmpty(File &file, uint32_t offset) {
    if (offset == 0 || file.size() <= offset) return true;
    uint8_t buffer[16];
    uint32_t len = std::min<uint32_t>(sizeof(buffer), file.size() - offset);
    if (!readSdBytes(file, offset, buffer, len)) return true;
    bool allFF = true, allZero = true;
    for (uint32_t i = 0; i < len; ++i) {
        if (buffer[i] != 0xFF) allFF = false;
        if (buffer[i] != 0x00) allZero = false;
    }
    return allFF || allZero;
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
    File &file, const String &path, uint32_t appSize, uint32_t appOffset,
    std::vector<LauncherInstallDataPartition> &dataPartitions
) {
    String error;
    LauncherPartitionTable table;
    if (!launcherPartitionReadCurrent(table, &error)) {
        displayError(error.length() ? error : "Partition read failed");
        return false;
    }

    if (appSize == 0 || appOffset + appSize > file.size()) {
        displayError("Invalid app image");
        return false;
    }

    String appLabel = launcherPartitionNextAppLabel(table, installedAppNameFromPath(path));
    LauncherPartitionEntry appEntry;

    if (!launcherSelectInstallLayout(table, appSize, appLabel, dataPartitions, appEntry, error)) {
        launcherConsolePrintf("SD install layout failed: %s\n", error.c_str());
        displayError(error.length() ? error : "No install space");
        return false;
    }
    if (!launcherPartitionValidate(table, &error)) {
        displayError(error.length() ? error : "Invalid table");
        return false;
    }

    String appNum = generateAppNum(path);
    bool shouldRestore = false;
    BackupInstallInfo prevInstall = loadInstalledFromConfig(appNum);
    if (!prevInstall.appNum.isEmpty()) {
        std::vector<String> restorable;
        for (const auto &bp : prevInstall.partitions) {
            if (!bp.lastBackupPath.isEmpty() && SDM.exists(bp.lastBackupPath)) restorable.push_back(bp.label);
        }
        if (!restorable.empty()) {
            if (autoBackup) {
                int choice = -1;
                std::vector<Option> opts = {
                    {"Restore Data",  [&]() { choice = 0; }},
                    {"Fresh Install", [&]() { choice = 1; }},
                };
                displayRedStripe("Previous backup found. Restore?");
                loopOptions(opts);
                shouldRestore = (choice == 0);
            } else {
                shouldRestore = true;
            }
        }
    }

    pauseSdInstallInput();
    bool success = false;
    prog_handler = 0;
    if (!flashRawFromSd(file, appOffset, appSize, appEntry, true)) {
        displayError(String("APP: ") + launcherUpdateLastErrorName());
        goto DONE;
    }

    for (const auto &dp : dataPartitions) {
        if (!dp.hasEntry || dp.copySize == 0) continue;
        if (shouldRestore) {
            bool willRestore = false;
            for (const auto &bp : prevInstall.partitions) {
                if (bp.label == dp.label && !bp.lastBackupPath.isEmpty() && SDM.exists(bp.lastBackupPath)) {
                    willRestore = true;
                    break;
                }
            }
            if (willRestore) continue;
        }
        const char *typeStr = dp.subtype == 0x81 ? "FAT" : dp.subtype == 0x83 ? "LittleFS" : "SPIFFS";
        displayRedStripe(String("Installing ") + typeStr);
        prog_handler = 1;
        const uint32_t copySize = dp.copySize > dp.entry.size ? dp.entry.size : dp.copySize;
        if (!flashRawFromSd(file, dp.sourceOffset, copySize, dp.entry, false)) {
            displayError(String(typeStr) + ": " + launcherUpdateLastErrorName());
            goto DONE;
        }
    }

    displayRedStripe("Writing table");
    if (!launcherPartitionWriteGeneratedTable(table, &error)) {
        displayError(error.length() ? error : "Table failed");
        goto DONE;
    }

    displayRedStripe("Setting boot");
    if (!launcherPartitionSetOtaBoot(table, appEntry.subtype, &error)) {
        displayError(error.length() ? error : "Boot failed");
        goto DONE;
    }

    if (shouldRestore) {
        for (const auto &bp : prevInstall.partitions) {
            if (!bp.lastBackupPath.isEmpty() && SDM.exists(bp.lastBackupPath)) {
                // Prefer direct write using the freshly written partition table entry,
                // since the IDF partition cache still reflects the old table.
                bool restored = false;
                for (const auto &dp : dataPartitions) {
                    if (dp.label == bp.label && dp.hasEntry) {
                        restored = restorePartitionFromBackupDirect(
                            bp.label.c_str(), bp.lastBackupPath.c_str(), dp.entry.offset, dp.entry.size
                        );
                        break;
                    }
                }
                if (!restored) {
                    restored = restorePartitionFromBackup(bp.label.c_str(), bp.lastBackupPath.c_str());
                }
                if (!restored) { log_w("[Restore] Failed: %s", bp.label.c_str()); }
            }
        }
    }

    {
        std::vector<String> fatLabels;
        String registeredSpiffsLabel;
        for (const auto &dp : dataPartitions) {
            if (!dp.hasEntry) continue;
            if (dp.subtype == 0x81) fatLabels.push_back(dp.label);
            else registeredSpiffsLabel = dp.label;
        }
        launcherSaveInstalledAppMetadata(
            table, appEntry, path, installedAppNameFromPath(path), fatLabels, registeredSpiffsLabel
        );
        saveIntoNVS();

        BackupInstallInfo bkInfo;
        bkInfo.appNum = appNum;
        bkInfo.sdFilepath = path;
        bkInfo.appName = installedAppNameFromPath(path);
        for (const auto &dp : dataPartitions) {
            if (!dp.hasEntry) continue;
            BackupPartitionInfo part;
            part.label = dp.label;
            part.type = dp.subtype == 0x81 ? "FAT" : dp.subtype == 0x83 ? "LittleFS" : "SPIFFS";
            for (const auto &bp : prevInstall.partitions) {
                if (bp.label == part.label && !bp.lastBackupPath.isEmpty()) {
                    part.lastBackupPath = bp.lastBackupPath;
                    break;
                }
            }
            bkInfo.partitions.push_back(part);
        }
        saveInstalledToConfig(bkInfo);
    }

    success = true;

DONE:
    resumeSdInstallInput();
    return success;
}

/***************************************************************************************
** Function name: updateFromSD
** Description:   this function analyse the .bin and calls installFromSdDynamic
***************************************************************************************/
void updateFromSD(const String &path) {
    uint8_t partitionEntry[LAUNCHER_PARTITION_ENTRY_SIZE];
    uint32_t app_size = 0;
    uint32_t app_offset = 0;
    std::vector<LauncherInstallDataPartition> dataPartitions;

    File file = SDM.open(path);

    if (!file) goto Exit;
    if (!file.seek(0x8000)) goto Exit;
    file.read(partitionEntry, 16);

    if (partitionEntry[0] != 0xAA || partitionEntry[1] != 0x50 || partitionEntry[2] != 0x01) {
        app_size = effectiveSdAppSize(file, 0, file.size());
        if (!installFromSdDynamic(file, path, app_size, 0, dataPartitions)) { goto Exit; }
        file.close();
        tft->fillScreen(BGCOLOR);

        return (void)releaseHeapObjectsAndReboot();
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

            if (partitionEntry[0x02] == 0x01 && (partitionEntry[3] == 0x82 || partitionEntry[3] == 0x83)) {
                LauncherInstallDataPartition dp;
                dp.subtype = partitionEntry[3];
                dp.sourceOffset = readLe32(partitionEntry + 0x04);
                const uint32_t declaredSize = readLe32(partitionEntry + 0x08);
                String declaredLabel = readPartitionLabel(partitionEntry);
                dp.label = declaredLabel.isEmpty() ? "spiffs" : declaredLabel;
                const bool partitionEmpty = sdPartitionIsEmpty(file, dp.sourceOffset);
                // accept data partitions as they are if not "spiffs", unless there's no actual
                // payload to justify the declared size
                if (partitionEmpty && declaredSize <= LAUNCHER_DEFAULT_SPIFFS_THRESHOLD) {
                    dp.partitionSize = LAUNCHER_DEFAULT_SPIFFS_SIZE;
                } else if (dp.label != "spiffs" && declaredSize > LAUNCHER_DEFAULT_SPIFFS_SIZE) {
                    dp.partitionSize = declaredSize;
                } else if (declaredSize > LAUNCHER_DEFAULT_SPIFFS_THRESHOLD) {
                    dp.partitionSize = LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE;
                } else {
                    dp.partitionSize = LAUNCHER_DEFAULT_SPIFFS_SIZE;
                }
                dp.copySize = partitionEmpty
                                  ? 0
                                  : boundedSdPartitionPayload(
                                        file,
                                        dp.sourceOffset,
                                        declaredSize,
                                        dp.partitionSize == LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE
                                            ? declaredSize
                                            : dp.partitionSize
                                    );
                if (file.size() < dp.sourceOffset) {
                    dp.copySize = 0;
                    launcherConsolePrintf(
                        "Found SPIFFS table entry without payload: create 0x%06X, copy 0\n", dp.partitionSize
                    );
                } else if (partitionEmpty) {
                    launcherConsolePrintf(
                        "Found empty %s partition at 0x%06X: create 0x%06X, copy 0\n",
                        dp.label.c_str(),
                        dp.sourceOffset,
                        dp.partitionSize
                    );
                }
                dataPartitions.push_back(dp);
            }

            if (partitionEntry[0x02] == 0x01 && partitionEntry[3] == 0x81) {
                LauncherInstallDataPartition dp;
                dp.subtype = 0x81;
                dp.label = readPartitionLabel(partitionEntry);
                uint32_t declaredSize = readLe32(partitionEntry + 0x08);
                dp.sourceOffset = readLe32(partitionEntry + 0x04);
                uint32_t availableSize =
                    dp.sourceOffset != 0 && file.size() > dp.sourceOffset ? file.size() - dp.sourceOffset : 0;
                LauncherPartitionPayloadPlan payload =
                    launcherPartitionFatPayloadPlan(dp.label.c_str(), declaredSize, 0, availableSize);
                dp.partitionSize = payload.partitionSize;
                dp.copySize = payload.copySize;
                dataPartitions.push_back(dp);
                launcherConsolePrintf(
                    "Found FAT %s at 0x%06X: create 0x%06X, copy 0x%06X of declared 0x%06X\n",
                    dp.label.c_str(),
                    dp.sourceOffset,
                    payload.partitionSize,
                    payload.copySize,
                    declaredSize
                );
            }
        }

        prog_handler = 0; // Install flash update
        {
            auto spiffsIt = std::find_if(
                dataPartitions.begin(), dataPartitions.end(), [](const LauncherInstallDataPartition &d) {
                    return d.subtype != 0x81 && d.label == "spiffs";
                }
            );
            if (spiffsIt != dataPartitions.end()) {
                if (!askSpiffs) {
                    spiffsIt->copySize = 0;
                } else if (spiffsIt->copySize > 0) {
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
                    if (!copySpiffs) spiffsIt->copySize = 0;
                    tft->fillRoundRect(6, 6, tftWidth - 12, tftHeight - 12, 5, BGCOLOR);
                }
            }
        }

        log_i("Appsize: %d", app_size);
        log_i("Data partitions: %d", dataPartitions.size());

        if (!installFromSdDynamic(file, path, app_size, app_offset, dataPartitions)) { goto Exit; }
        displayMsg("Complete");

        return (void)releaseHeapObjectsAndReboot();
    }
Exit:
    displayError("Update Error.");
}

/***************************************************************************************
** Function name: performDATAUpdate
** Description:   this function performs the update of any data partition by label
***************************************************************************************/
bool performDATAUpdate(Stream &updateSource, size_t updateSize, const char *label) {
    prog_handler = 1;
    progressHandler(0, 500);
    displayRedStripe("Updating partition");
    log_i("Updating DATA partition: %s", label);

    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!partition) {
        log_i("FAIL: partition not found: %s", label);
        return false;
    }

    if (!launcherRawUpdateStream(
            updateSource, partition->address, partition->size, updateSize, false, progressHandler
        )) {
        log_i("FAIL updating %s", label);
        return false;
    }

    String patchError;
    if (!launcherPatchReducedLittlefsSuperblocks(partition->address, partition->size, &patchError)) {
        launcherConsolePrintf(
            "LittleFS patch failed after DATA restore label=%s offset=0x%08X size=0x%08X: %s\n",
            label,
            partition->address,
            partition->size,
            patchError.c_str()
        );
        return false;
    }

    log_i("Success updating %s", label);
    return true;
}
