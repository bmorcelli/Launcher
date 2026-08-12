#include "partitioner.h"
#include "app_registry.h"
#include "backup_manager.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "idf/idf_update.h"
#include "idf/launcher_platform.h"
#include "littlefs_patch.h"
#include "mykeyboard.h"
#include "partition_table_model.h"
#include "ram_profile.h"
#include "sd_functions.h"
#include "utils.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <globals.h>

// Define o tamanho da partição
#define PARTITION_SIZE 4096

namespace {
String hex32(uint32_t value) {
    char out[11] = {0};
    snprintf(out, sizeof(out), "0x%lX", static_cast<unsigned long>(value));
    return String(out);
}

String sizeText(uint32_t value) {
    if ((value % 0x100000) == 0) return String(value / 0x100000) + "MB";
    if ((value % 0x400) == 0) return String(value / 0x400) + "KB";
    return String(value) + "B";
}

uint32_t alignUpLocal(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t alignDownLocal(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return value & ~(alignment - 1);
}

uint32_t partitionAlignment(uint8_t type, uint8_t subtype = 0xFF) {
    return launcherPartitionAlignment(type, subtype);
}

bool confirmAction(const String &message) {
    bool confirmed = false;
    std::vector<Option> confirmOptions = {
        {"Confirm", [&]() { confirmed = true; } },
        {"Cancel",  [&]() { confirmed = false; }},
    };
    displayRedStripe(message);
    loopOptions(confirmOptions);
    return confirmed;
}

String nextOtaAppLabel(const LauncherPartitionTable &table) {
    int highest = 0;
    for (const LauncherPartitionEntry &entry : table.entries) {
        if (strncmp(entry.label, "app", 3) != 0) continue;

        const char *cursor = entry.label + 3;
        if (*cursor == '\0') continue;

        bool numeric = true;
        int value = 0;
        while (*cursor) {
            if (*cursor < '0' || *cursor > '9') {
                numeric = false;
                break;
            }
            value = (value * 10) + (*cursor - '0');
            cursor++;
        }
        if (numeric && value > highest) highest = value;
    }
    return "app" + String(highest + 1);
}

int sliderXForOffset(uint32_t offset, uint32_t minOffset, uint32_t maxOffset, int barX, int barWidth) {
    if (maxOffset <= minOffset) return barX;
    uint64_t numerator = static_cast<uint64_t>(offset - minOffset) * static_cast<uint64_t>(barWidth);
    return barX + static_cast<int>(numerator / (maxOffset - minOffset));
}

uint32_t
sliderOffsetForX(int x, uint32_t minOffset, uint32_t maxOffset, uint32_t step, int barX, int barWidth) {
    if (maxOffset <= minOffset || barWidth <= 0) return minOffset;
    if (x <= barX) return minOffset;
    if (x >= barX + barWidth) return maxOffset;

    uint64_t raw =
        minOffset +
        (static_cast<uint64_t>(x - barX) * static_cast<uint64_t>(maxOffset - minOffset)) / barWidth;
    if (step == 0) return static_cast<uint32_t>(raw);

    uint32_t relative = static_cast<uint32_t>(raw - minOffset);
    uint32_t steps = (relative + (step / 2)) / step;
    uint32_t snapped = minOffset + steps * step;
    if (snapped > maxOffset) snapped = maxOffset;
    return snapped;
}

void applySliderTouch(
    int x, uint32_t minOffset, uint32_t maxOffset, uint32_t step, uint32_t minSize, uint32_t &start,
    uint32_t &end, int &moveStart, bool &moveSelected
) {
    const int barX = 14;
    const int barW = tftWidth > 34 ? tftWidth - 28 : tftWidth - 4;
    const int startX = sliderXForOffset(start, minOffset, maxOffset, barX, barW);
    const int endX = sliderXForOffset(end, minOffset, maxOffset, barX, barW);

    moveStart = abs(x - startX) <= abs(x - endX) ? 1 : 0;
    moveSelected = true;

    uint32_t touchedOffset = sliderOffsetForX(x, minOffset, maxOffset, step, barX, barW);
    if (moveStart) {
        uint32_t maxStart = end > minSize ? end - minSize : minOffset;
        if (touchedOffset < minOffset) touchedOffset = minOffset;
        if (touchedOffset > maxStart) touchedOffset = maxStart;
        start = alignDownLocal(touchedOffset, step);
    } else {
        uint32_t minEnd = start + minSize;
        if (touchedOffset < minEnd) touchedOffset = minEnd;
        if (touchedOffset > maxOffset) touchedOffset = maxOffset;
        end = alignUpLocal(touchedOffset, step);
        if (end > maxOffset) end = maxOffset;
    }
}

void drawRangeSlider(
    const String &title, uint32_t minOffset, uint32_t maxOffset, uint32_t start, uint32_t end, uint32_t step,
    int moveStart
) {
    tft->fillScreen(BGCOLOR);
    resetTftDisplay(8, 8, FGCOLOR, FP, BGCOLOR, BGCOLOR);
    tftprintln(title, 8);
    tftprintln(String(moveStart >= 0 ? (moveStart ? "Moving: Start" : "Moving: End") : "Moving: ----"), 8);
    tftprintln(String("Range: ") + hex32(start) + " - " + hex32(end), 8);
    tftprintln(String("Size: ") + hex32(end - start) + " (" + sizeText(end - start) + ")", 8);
    tftprintln(String("Step: ") + hex32(step), 8);

    const int barX = 14;
    const int barW = tftWidth > 34 ? tftWidth - 28 : tftWidth - 4;
    const int barY = tftHeight / 2;
    const int startX = sliderXForOffset(start, minOffset, maxOffset, barX, barW);
    const int endX = sliderXForOffset(end, minOffset, maxOffset, barX, barW);

    tft->drawRect(barX, barY, barW, 10, DARKGREY);
    if (endX > startX) tft->fillRect(startX, barY + 2, endX - startX, 6, ALCOLOR);
    tft->fillRect(
        startX - 2, barY - 6, 5, 22, moveStart >= 0 ? (moveStart ? FGCOLOR : LIGHTGREY) : LIGHTGREY
    );
    tft->fillRect(endX - 2, barY - 6, 5, 22, moveStart >= 0 ? (moveStart ? LIGHTGREY : FGCOLOR) : LIGHTGREY);

    tft->setTextColor(moveStart >= 0 ? FGCOLOR : BGCOLOR, moveStart >= 0 ? BGCOLOR : FGCOLOR);
    tft->drawCentreString(" Confirm/Exit ", tftWidth / 2, barY + 16, 1);

    tft->setTextColor(ALCOLOR, BGCOLOR);

    // Spread the three hints into left/centre/right columns so they
    // line up cleanly across the width (matches the touch footer layout).
    const int hintY = tftHeight - (LH * FP + 8);
    tft->drawString("[Prev/Next move]", 8, hintY);
    tft->drawCentreString("[Sel ok]", tftWidth / 2, hintY, 1);
    tft->drawRightString("[Esc cancel]", tftWidth - 8, hintY, 1);
}

bool rangeSlider(
    const String &title, uint32_t minOffset, uint32_t maxOffset, uint32_t step, uint32_t minSize,
    uint32_t &start, uint32_t &end
) {
    if (step == 0) step = LAUNCHER_FLASH_SECTOR_SIZE;
    minOffset = alignUpLocal(minOffset, step);
    maxOffset = alignDownLocal(maxOffset, step);
    start = alignUpLocal(start, step);
    end = alignDownLocal(end, step);

    if (maxOffset <= minOffset || maxOffset - minOffset < minSize) {
        displayError("No usable range");
        return false;
    }
    if (start < minOffset) start = minOffset;
    if (end > maxOffset) end = maxOffset;
    if (end <= start || end - start < minSize) end = start + minSize;
    if (end > maxOffset) {
        end = maxOffset;
        start = end - minSize;
    }

    int moveStart = -1;
    bool moveSelected = false;

    bool redraw = true;
    while (true) {
        if (redraw) {
            if (moveStart > 1) moveStart = -1;
            if (moveStart < -1) moveStart = 1;
            drawRangeSlider(title, minOffset, maxOffset, start, end, step, moveStart);
            redraw = false;
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            const int touchX = touchPoint.x;
            const int touchY = touchPoint.y;
            touchPoint.Clear();

            const int barY = tftHeight / 2;
            const int sliderTop = barY - 16;
            const int sliderBottom = barY + 14;
            const int confirmTop = barY + 14;
            const int confirmBottom = barY + 38;

            if (touchY >= confirmTop && touchY <= confirmBottom) return true;
            if (touchY >= sliderTop && touchY <= sliderBottom) {
                applySliderTouch(
                    touchX, minOffset, maxOffset, step, minSize, start, end, moveStart, moveSelected
                );
                redraw = true;
                continue;
            }
        }
#endif

        if (check(PrevPress) || check(UpPress)) {
            if (moveSelected) {
                if (moveStart) {
                    if (start >= minOffset + step) start -= step;
                } else if (end >= start + minSize + step) {
                    end -= step;
                }
            } else {
                moveStart--;
            }
            redraw = true;
        }
        if (check(NextPress) || check(DownPress)) {
            if (moveSelected) {
                if (moveStart) {
                    if (start + minSize + step <= end) start += step;
                } else if (end + step <= maxOffset) {
                    end += step;
                }
            } else {
                moveStart++;
            }
            redraw = true;
        }
        if (check(SelPress)) {
            if (moveSelected) {
                moveSelected = false;
                if (moveStart < 0) return true;
            } else {
                moveSelected = true;
            }
            redraw = true;
        }
        if (check(EscPress) || returnToMenu) return false;
        yield();
    }
}

String partitionTypeName(const LauncherPartitionEntry &entry) {
    if (entry.type == 0x00) return "APP";
    if (entry.type == 0x01) return "DATA";
    if (entry.type == 0x02) return "BOOT";
    if (entry.type == 0x03) return "PTBL";
    return "UNK";
}

String partitionSubtypeName(const LauncherPartitionEntry &entry) {
    if (entry.type == 0x00) {
        if (entry.subtype == 0x00) return "factory";
        if (entry.subtype >= 0x10 && entry.subtype <= 0x1F) return "ota_" + String(entry.subtype - 0x10);
        if (entry.subtype == 0x20) return "test";
    }
    if (entry.type == 0x01) {
        if (entry.subtype == 0x00) return "ota";
        if (entry.subtype == 0x01) return "phy";
        if (entry.subtype == 0x02) return "nvs";
        if (entry.subtype == 0x03) return "coredump";
        if (entry.subtype == 0x81) return "fat";
        if (entry.subtype == 0x82) return "spiffs";
        if (entry.subtype == 0x83) return "littlefs";
    }
    char out[5] = {0};
    snprintf(out, sizeof(out), "%02X", entry.subtype);
    return String(out);
}

const char *dataSubtypeName(uint8_t subtype) {
    if (subtype == ESP_PARTITION_SUBTYPE_DATA_FAT) return "FAT";
    if (subtype == 0x83) return "LittleFS";
    return "SPIFFS";
}

bool isProtectedPartition(const LauncherPartitionEntry &entry) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->address == entry.offset) return true;
    if (entry.isFactoryOrTestApp()) return true;
    if (entry.type == 0x01 && entry.subtype <= 0x05) return true;
    return false;
}

String partitionRow(const LauncherPartitionEntry &entry) {
    String row = isProtectedPartition(entry) ? "* " : "  ";
    row += String(entry.label);
    row += " ";
    row += partitionTypeName(entry);
    row += "/";
    row += partitionSubtypeName(entry);
    row += " ofs=";
    row += hex32(entry.offset);
    row += " size=";
    row += hex32(entry.size);
    row += " (" + sizeText(entry.size) + ")";
    return row;
}

void waitForSelectRelease() {
    while (!check(SelPress)) yield();
    while (check(SelPress)) yield();
}

void showPartitionDetails(const LauncherPartitionEntry &entry) {
    tft->fillScreen(BGCOLOR);
    resetTftDisplay(8, 8, FGCOLOR, FP, BGCOLOR, BGCOLOR);
    tftprintln("Partition", 8);
    tftprintln(String("Label: ") + entry.label, 8);
    tftprintln(String("Type: ") + partitionTypeName(entry), 8);
    tftprintln(String("Subtype: ") + partitionSubtypeName(entry), 8);
    tftprintln(String("Offset: ") + hex32(entry.offset), 8);
    tftprintln(String("Size: ") + hex32(entry.size) + " (" + sizeText(entry.size) + ")", 8);
    tftprintln(String("Flags: ") + hex32(entry.flags), 8);
    tftprintln(String("Status: ") + (isProtectedPartition(entry) ? "protected" : "editable"), 8);
    tft->setTextColor(ALCOLOR);
    tftprintln("Press Select", 8);
    launcherConsolePrintf(
        "Partition label=%s type=0x%02X subtype=0x%02X offset=0x%08X size=0x%08X flags=0x%08X protected=%d\n",
        entry.label,
        entry.type,
        entry.subtype,
        entry.offset,
        entry.size,
        entry.flags,
        isProtectedPartition(entry)
    );
    TouchFooter();
    waitForSelectRelease();
}

void showFreeRangeDetails(const LauncherPartitionRange &range) {
    tft->fillScreen(BGCOLOR);
    resetTftDisplay(8, 8, FGCOLOR, FP, BGCOLOR, BGCOLOR);
    tftprintln("Free Range", 8);
    tftprintln(String("Offset: ") + hex32(range.offset), 8);
    tftprintln(String("Size: ") + hex32(range.size) + " (" + sizeText(range.size) + ")", 8);
    tftprintln(String("End: ") + hex32(range.offset + range.size), 8);
    tft->setTextColor(ALCOLOR);
    tftprintln("Press Select", 8);
    launcherConsolePrintf(
        "Free range offset=0x%08X size=0x%08X end=0x%08X\n",
        range.offset,
        range.size,
        range.offset + range.size
    );
    TouchFooter();
    waitForSelectRelease();
}

int findEntryIndex(const LauncherPartitionTable &table, const LauncherPartitionEntry &target) {
    for (size_t i = 0; i < table.entries.size(); ++i) {
        const LauncherPartitionEntry &entry = table.entries[i];
        if (entry.type == target.type && entry.subtype == target.subtype && entry.offset == target.offset &&
            strncmp(entry.label, target.label, 16) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool validateOrShow(const LauncherPartitionTable &table) {
    String error;
    if (launcherPartitionValidate(table, &error)) return true;
    launcherConsolePrintf("Partition validation failed: %s\n", error.c_str());
    displayError(error.length() ? error : "Invalid partition");
    return false;
}

bool compactOrShow(LauncherPartitionTable &table) {
    String error;
    if (launcherPartitionCompact(table, &error)) return true;
    launcherConsolePrintf("Partition compact failed: %s\n", error.c_str());
    displayError(error.length() ? error : "Compact failed");
    return false;
}

bool editPartitionSize(LauncherPartitionTable &table, const LauncherPartitionEntry &target) {
    int index = findEntryIndex(table, target);
    if (index < 0) return false;
    if (isProtectedPartition(table.entries[index])) {
        displayError("Protected partition");
        return false;
    }

    const uint32_t alignment = partitionAlignment(table.entries[index].type, table.entries[index].subtype);
    uint32_t minOffset = LAUNCHER_PARTITION_TABLE_OFFSET + LAUNCHER_PARTITION_TABLE_SIZE;
    uint32_t maxOffset = table.flashSize;
    const uint32_t currentStart = table.entries[index].offset;
    const uint32_t currentEnd = table.entries[index].offset + table.entries[index].size;

    for (size_t i = 0; i < table.entries.size(); ++i) {
        if (static_cast<int>(i) == index) continue;
        const LauncherPartitionEntry &entry = table.entries[i];
        const uint32_t entryEnd = entry.offset + entry.size;
        if (entryEnd <= currentStart && entryEnd > minOffset) minOffset = entryEnd;
        if (entry.offset >= currentEnd && entry.offset < maxOffset) maxOffset = entry.offset;
    }

    uint32_t start = currentStart;
    uint32_t end = currentEnd;
    if (!rangeSlider("Partition Range", minOffset, maxOffset, alignment, alignment, start, end)) return false;

    LauncherPartitionTable edited = table;
    edited.entries[index].offset = start;
    edited.entries[index].size = end - start;
    if (!validateOrShow(edited)) return false;
    if (!compactOrShow(edited)) return false;
    table = edited;
    return true;
}

bool removePartition(LauncherPartitionTable &table, const LauncherPartitionEntry &target) {
    int index = findEntryIndex(table, target);
    if (index < 0) return false;
    if (isProtectedPartition(table.entries[index])) {
        displayError("Protected partition");
        return false;
    }
    if (!confirmAction("Remove partition?")) return false;

    LauncherPartitionTable edited = table;
    edited.entries.erase(edited.entries.begin() + index);
    if (!validateOrShow(edited)) return false;
    if (!compactOrShow(edited)) return false;
    table = edited;
    return true;
}

bool formatPartition(const LauncherPartitionEntry &entry, bool dirty) {
    if (dirty) {
        displayError("Apply changes first");
        return false;
    }
    if (isProtectedPartition(entry) || !entry.isData()) {
        displayError("Cannot format");
        return false;
    }
    if (!confirmAction("Erase partition?")) return false;

    displayRedStripe("Formatting...");
    bool ok = launcherRawPrepareDataPartition(entry.offset, entry.size);
    displayMsg(ok ? "Formatted" : launcherUpdateLastErrorName());
    return ok;
}

bool findFreeSliderRange(
    const LauncherPartitionTable &table, uint32_t requiredSize, uint32_t alignment,
    LauncherPartitionRange &range, String *error
);

bool normalizeFreeSliderRange(
    const LauncherPartitionRange &candidate, uint32_t requiredSize, uint32_t alignment,
    LauncherPartitionRange &range
) {
    if (alignment == 0) alignment = LAUNCHER_FLASH_SECTOR_SIZE;
    const uint32_t alignedOffset = alignUpLocal(candidate.offset, alignment);
    const uint32_t candidateEnd = candidate.offset + candidate.size;
    if (candidateEnd <= alignedOffset) return false;

    const uint32_t alignedEnd = alignDownLocal(candidateEnd, alignment);
    if (alignedEnd <= alignedOffset) return false;

    const uint32_t usableSize = alignedEnd - alignedOffset;
    if (usableSize < requiredSize) return false;

    range = {alignedOffset, usableSize};
    return true;
}

bool createPartition(
    LauncherPartitionTable &table, uint8_t type, uint8_t subtype, const char *defaultLabel,
    const LauncherPartitionRange *preferredRange = nullptr
) {
    String label;
    if (type == 0x00) {
        label = nextOtaAppLabel(table);
    } else {
        label = keyboard(defaultLabel, 15, "Partition label");
        if (label == "" || label == String(KEY_ESCAPE)) return false;
    }

    const uint32_t alignment = partitionAlignment(type, subtype);
    uint32_t requestedSize = type == 0x00 ? 0x100000 : launcherPartitionDefaultFatSize(label.c_str());
    if (subtype == 0x82) requestedSize = 0x10000;
    requestedSize = alignUpLocal(requestedSize, alignment);

    LauncherPartitionRange range;
    String error;
    if (preferredRange) {
        if (!normalizeFreeSliderRange(*preferredRange, requestedSize, alignment, range)) {
            displayError("No space in range");
            return false;
        }
    } else {
        if (!findFreeSliderRange(table, requestedSize, alignment, range, &error)) {
            launcherConsolePrintf("Partition range selection failed: %s\n", error.c_str());
            displayError(error.length() ? error : "No free range");
            return false;
        }
    }

    uint32_t start = range.offset;
    uint32_t end = range.offset + range.size;
    if (!rangeSlider(
            String("Create ") + label,
            range.offset,
            range.offset + range.size,
            alignment,
            alignment,
            start,
            end
        )) {
        return false;
    }

    LauncherPartitionTable edited = table;
    LauncherPartitionEntry created;
    created.type = type;
    created.subtype = subtype;
    created.offset = start;
    created.size = end - start;
    created.flags = 0;
    memset(created.label, 0, sizeof(created.label));
    strncpy(created.label, label.c_str(), 15);

    if (type == 0x00) {
        int nextSubtype = launcherPartitionNextOtaSubtype(table);
        if (nextSubtype < 0) {
            displayError("No OTA slot");
            return false;
        }
        created.subtype = static_cast<uint8_t>(nextSubtype);
    }

    if (!launcherPartitionAdd(edited, created, &error)) {
        launcherConsolePrintf("Partition create failed: %s\n", error.c_str());
        displayError(error.length() ? error : "Create failed");
        return false;
    }
    if (!compactOrShow(edited)) return false;
    table = edited;
    return true;
}

bool rangeHasFreeSpaceFor(const LauncherPartitionRange &range, uint32_t requiredSize, uint32_t alignment) {
    LauncherPartitionRange normalized;
    return normalizeFreeSliderRange(range, requiredSize, alignment, normalized);
}

bool findFreeSliderRange(
    const LauncherPartitionTable &table, uint32_t requiredSize, uint32_t alignment,
    LauncherPartitionRange &range, String *error
) {
    for (const LauncherPartitionRange &candidate : launcherPartitionFreeRanges(table)) {
        if (normalizeFreeSliderRange(candidate, requiredSize, alignment, range)) return true;
    }

    if (error) *error = "No free partition range large enough";
    return false;
}

bool wipeFlashMemory() {
    if (!confirmAction("Wipe flash memory?")) return false;

    String error;
    LauncherPartitionTable current;
    if (!launcherPartitionReadCurrent(current, &error)) {
        launcherConsolePrintf("Partition table read failed: %s\n", error.c_str());
        displayError(error.length() ? error : "Read failed");
        return false;
    }

    LauncherPartitionTable target;
    target.flashSize = current.flashSize;
    for (const LauncherPartitionEntry &entry : current.entries) {
        if (isProtectedPartition(entry)) target.entries.push_back(entry);
    }

    if (!validateOrShow(target)) return false;

    displayRedStripe("Clearing App registry");
    if (!launcherClearAppRegistry()) {
        displayError("Registry clear failed");
        return false;
    }

    displayRedStripe("Writing table");
    if (!launcherPartitionWriteGeneratedTable(target, &error)) {
        launcherConsolePrintf("Partition table write failed: %s\n", error.c_str());
        displayError(error.length() ? error : "Write failed");
        return false;
    }

    displayRedStripe("Restart needed");
    waitForSelectRelease();

    return releaseHeapObjectsAndReboot();
}

bool applyPartitionChanges(const LauncherPartitionTable &table) {
    LauncherPartitionTable target = table;
    if (!compactOrShow(target)) return false;
    if (!validateOrShow(target)) return false;
    if (!confirmAction("Write table?")) return false;

    String error;
    LauncherPartitionTable current;
    if (!launcherPartitionReadCurrent(current, &error)) {
        launcherConsolePrintf("Partition table read failed: %s\n", error.c_str());
        displayError(error.length() ? error : "Read failed");
        return false;
    }

    displayRedStripe("Optimizing flash");
    if (!launcherPartitionMigrateMovedData(current, target, &error)) {
        launcherConsolePrintf("Partition data move failed: %s\n", error.c_str());
        displayError(error.length() ? error : "Move failed");
        return false;
    }

    displayRedStripe("Writing table");
    if (!launcherPartitionWriteGeneratedTable(target, &error)) {
        launcherConsolePrintf("Partition table write failed: %s\n", error.c_str());
        displayError(error.length() ? error : "Write failed");
        return false;
    }

    displayRedStripe("Restart needed");
    waitForSelectRelease();

    return releaseHeapObjectsAndReboot();
}
} // namespace

void partList() {
    RAM_LOG("partList-start");
    int idx = 0;
    LauncherPartitionTable table;
    bool dirty = false;
    String error;
    returnToMenu = false;
    if (!launcherPartitionReadCurrent(table, &error)) {
        launcherConsolePrintf("Partition table read failed: %s\n", error.c_str());
        displayError(error.length() ? error : "Partition read failed");
        return;
    }

    while (idx >= 0 && returnToMenu == false) {
        launcherConsolePrintf(
            "Partitions found: %d, flash size: 0x%08X\n", table.entries.size(), table.flashSize
        );
        std::vector<Option> partitionOptions;
        partitionOptions.push_back({"* Partition Scheme", []() { yield(); }});
        for (const LauncherPartitionEntry &entry : table.entries) {
            if (entry.offset < 0x10000) continue; // Don't show system partitions
            launcherConsolePrintf(
                "%s label=%s type=%s subtype=%s offset=%s size=%s (%s)\n",
                isProtectedPartition(entry) ? "*" : " ",
                entry.label,
                partitionTypeName(entry).c_str(),
                partitionSubtypeName(entry).c_str(),
                hex32(entry.offset).c_str(),
                hex32(entry.size).c_str(),
                sizeText(entry.size).c_str()
            );
            partitionOptions.push_back(
                {partitionRow(entry),
                 [&table, &dirty, entry]() {
                     int selected = 100;
                     String linkedAppNum =
                         entry.isData() ? findAppNumByPartitionLabel(String(entry.label)) : String();
                     std::vector<Option> entryOptions = {};
                     entryOptions.push_back({"Details", [&]() { selected = 0; }});
                     if (entry.isData()) {
                         if (linkedAppNum.isEmpty())
                             entryOptions.push_back({"Associate to Bin", [&]() { selected = 7; }});
                         entryOptions.push_back({"Backup", [&]() { selected = 5; }});
                         entryOptions.push_back({"Restore data", [&]() { selected = 6; }});
                     }
                     if (!isProtectedPartition(entry)) {
                         entryOptions.push_back({"Edit Size", [&]() { selected = 1; }});
                         entryOptions.push_back({"Remove", [&]() { selected = 2; }});
                         entryOptions.push_back({"Format", [&]() { selected = 3; }});
                     }
                     entryOptions.push_back({"Back", [&]() { selected = 4; }});
                     loopOptions(entryOptions);
                     if (selected == 0) showPartitionDetails(entry);
                     else if (selected == 1) {
                         if (editPartitionSize(table, entry)) dirty = true;
                     } else if (selected == 2) {
                         if (removePartition(table, entry)) dirty = true;
                     } else if (selected == 3) {
                         formatPartition(entry, dirty);
                     } else if (selected == 5) {
                         if (!linkedAppNum.isEmpty()) {
                             String typeName = dataSubtypeName(entry.subtype);
                             String path = backupPartition(linkedAppNum, entry.label, typeName.c_str());
                             if (path.isEmpty()) {
                                 displayError("Backup failed");
                             } else {
                                 displayMsg("Backup saved!");
                             }
                         } else {
                             String outputPath = String("/bkp/") + entry.label;
                             dumpPartition(entry.label, outputPath.c_str());
                         }
                     } else if (selected == 6) {
                         restorePartition(entry.label);
                     } else if (selected == 7) {
                         String selectedBin = loopSD(false);
                         if (!selectedBin.isEmpty()) {
                             String appNum = generateAppNum(selectedBin);
                             BackupInstallInfo info = loadInstalledFromConfig(appNum);
                             if (info.appNum.isEmpty()) {
                                 info.appNum = appNum;
                                 info.sdFilepath = selectedBin;
                                 info.appName = launcherAppNameFromFile(selectedBin);
                             }
                             bool alreadyIn = false;
                             for (const auto &bp : info.partitions)
                                 if (bp.label == String(entry.label)) {
                                     alreadyIn = true;
                                     break;
                                 }
                             if (!alreadyIn) {
                                 BackupPartitionInfo bp;
                                 bp.label = String(entry.label);
                                 bp.type = dataSubtypeName(entry.subtype);
                                 info.partitions.push_back(bp);
                             }
                             saveInstalledToConfig(info);
                             displayError(("Linked to " + launcherAppNameFromFile(selectedBin)).c_str());
                         }
                     }
                 },
                 isProtectedPartition(entry) ? ALCOLOR : FGCOLOR}
            );
        }

        for (const LauncherPartitionRange &range : launcherPartitionFreeRanges(table)) {
            if (range.size == 0) continue;
            String row = "  FREE ofs=";
            row += hex32(range.offset);
            row += " size=";
            row += hex32(range.size);
            row += " (" + sizeText(range.size) + ")";
            partitionOptions.push_back(
                {row,
                 [&table, &dirty, range]() {
                     int selected = 100;
                     std::vector<Option> freeOptions;
                     freeOptions.push_back({"Details", [&]() { selected = 0; }});
                     if (rangeHasFreeSpaceFor(range, 0x100000, 0x10000)) {
                         freeOptions.push_back({"Add OTA", [&]() { selected = 1; }});
                     }
                     if (rangeHasFreeSpaceFor(
                             range, launcherPartitionDefaultFatSize("vfs"), LAUNCHER_FLASH_SECTOR_SIZE
                         )) {
                         freeOptions.push_back({"Add FAT", [&]() { selected = 2; }});
                     }
                     if (rangeHasFreeSpaceFor(range, 0x10000, 0x10000)) {
                         freeOptions.push_back({"Add SPIFFS", [&]() { selected = 3; }});
                     }
                     freeOptions.push_back({"Back", [&]() { selected = 4; }});

                     loopOptions(freeOptions);
                     if (selected == 0) showFreeRangeDetails(range);
                     else if (selected == 1) {
                         if (createPartition(table, 0x00, 0x10, "app", &range)) dirty = true;
                     } else if (selected == 2) {
                         if (createPartition(table, 0x01, 0x81, "vfs", &range)) dirty = true;
                     } else if (selected == 3) {
                         if (createPartition(table, 0x01, 0x82, "spiffs", &range)) dirty = true;
                     }
                 },
                 FGCOLOR}
            );
        }
        partitionOptions.push_back({"* = protected", []() { yield(); }});
        if (dirty) {
            partitionOptions.push_back({"Apply Changes", [&]() { applyPartitionChanges(table); }, ALCOLOR});
            partitionOptions.push_back(
                {"Discard Changes", [&]() {
                     if (confirmAction("Discard changes?")) {
                         String readError;
                         if (launcherPartitionReadCurrent(table, &readError)) dirty = false;
                         else { displayError(readError.length() ? readError : "Reload failed"); }
                     }
                 }}
            );
        }
        if (dev_mode) {
            partitionOptions.push_back(
                {"Wipe Flash memory",
                 [&]() {
                     if (wipeFlashMemory()) returnToMenu = true;
                 },
                 ALCOLOR}
            );
        }
        partitionOptions.push_back(
            {"Back",
             [&]() {
                 if (!dirty || confirmAction("Discard changes?")) returnToMenu = true;
             },
             ALCOLOR}
        );
        if (idx >= static_cast<int>(partitionOptions.size())) {
            idx = partitionOptions.empty() ? 0 : static_cast<int>(partitionOptions.size()) - 1;
        }
        idx = loopOptions(partitionOptions, false, ALCOLOR, BGCOLOR, false, idx);
        tft->fillScreen(BGCOLOR);
    }
}

void dumpPartition(const char *partitionLabel, const char *outputPath) {
    tft->fillScreen(BGCOLOR);
    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partitionLabel);
    if (partition == NULL) {
        launcherConsolePrintf("Partição %s não encontrada\n", partitionLabel);
        return;
    }

    setupSdCard();
    if (!SDM.exists("/bkp")) SDM.mkdir("/bkp");

    String output = outputPath;
    output += ".bin";
    int i = 0;
    while (SDM.exists(output)) {
        i++;
        output = String(outputPath) + String(i) + ".bin";
    }

    File outputFile = SDM.open(output.c_str(), FILE_WRITE, true);
    if (!outputFile) {
        launcherConsolePrintf("Falha ao abrir o arquivo %s no cartão SD\n", outputPath);
        return;
    }

    launcherConsolePrintf("Iniciando dump da partição %s para o arquivo %s\n", partitionLabel, outputPath);

    const size_t bufferSize = 1024; // Ajuste conforme necessário
    uint8_t buffer[1024];
    size_t bytesToRead = 0;
    esp_err_t result;
    progressHandler(0, 500);
    displayRedStripe("Backing up");
    for (size_t offset = 0; offset < partition->size; offset += bufferSize) {
        bytesToRead = (offset + bufferSize > partition->size) ? (partition->size - offset) : bufferSize;
        result = esp_partition_read(partition, offset, buffer, bytesToRead);
        if (result != ESP_OK) {
            launcherConsolePrintf(
                "Erro ao ler a partição %s no offset %d (código de erro: %d)\n",
                partitionLabel,
                offset,
                result
            );
            outputFile.close();
            return;
        }
        outputFile.write(buffer, bytesToRead);
        progressHandler(int(offset + bufferSize), partition->size);
    }
    outputFile.close();
    displayMsg("    Complete!    ");
    displayMsg(output);
    launcherConsolePrintf("Dump da partição %s para o arquivo %s concluído\n", partitionLabel, outputPath);

    bool attach = false;
    options = {
        {"Attach to a file", [&]() { attach = true; }      },
        {"Main Menu",        [=]() { returnToMenu = true; }}
    };
    loopOptions(options);

    if (attach) {
        String to = loopSD(true);
        attachPartition(output, to);
    }
}

void restorePartition(const char *partitionLabel) {
    String filepath = loopSD(true);
    tft->fillScreen(BGCOLOR);
    if (filepath == "") return;

    File source = SDM.open(filepath, "r");
    if (!source) {
        displayError("Can't open file");
        return;
    }
    bool restored = performDATAUpdate(source, source.size(), partitionLabel);
    source.close();
    if (!restored) {
        displayError(launcherUpdateLastErrorName());
        return;
    }
    launcherDelayMs(100);
    displayMsg("    Restored!    ");
}

#define TAG "Partitioneer"

#if CONFIG_IDF_TARGET_ESP32P4
#define TARGET_PARTITION ESP_PARTITION_SUBTYPE_APP_FACTORY
#else
#define TARGET_PARTITION ESP_PARTITION_SUBTYPE_APP_TEST
#endif
// Função principal
void partitionCrawler() {
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return;
    }

    if (running_partition->subtype == TARGET_PARTITION) {
        ESP_LOGI(
            TAG,
            "Running partition is %s partition, no action taken",
            TARGET_PARTITION == ESP_PARTITION_SUBTYPE_APP_TEST ? "TEST" : "FACTORY"
        );
        return;
    }

    displayRedStripe("Updating...");

    const esp_partition_t *test_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, TARGET_PARTITION, NULL);

    if (test_partition == NULL) {
        ESP_LOGE(TAG, "Failed to find test partition");
        return;
    }

    ESP_LOGI(TAG, "Erasing test partition");
    if (!launcherUpdateErasePartition(test_partition)) {
        ESP_LOGE(TAG, "Failed to erase test partition");
        return;
    }

    ESP_LOGI(TAG, "Copying running partition to test partition");
    progressHandler(0, 500);
    displayRedStripe("Launcher Update");
    if (!launcherUpdateCopyPartition(running_partition, test_partition, progressHandler)) {
        ESP_LOGE(TAG, "Failed to copy partition data");
        displayError("Use M5Burner!");
        return;
    }

    bool removedRunningOta = false;
    displayRedStripe("Updating table");
    if (!launcherUpdateRepairPartitionTable(running_partition->address, &removedRunningOta)) {
        displayError("Partition fix failed");
        return;
    }

    if (removedRunningOta) {
        ESP_LOGI(TAG, "Running OTA partition was removed from partition table, restarting");

        return (void)releaseHeapObjectsAndReboot();
    }

    if (running_partition->address == test_partition->address) {
        ESP_LOGW(
            TAG,
            "Running partition address matches target partition address 0x%08lX, skipping invalidation",
            static_cast<unsigned long>(running_partition->address)
        );

        return (void)releaseHeapObjectsAndReboot();
        return;
    }

    ESP_LOGI(TAG, "Writing 0x00 to first byte of the running partition (break OTA0 Launcher)");
    uint8_t zero_byte = 0x00;
    esp_err_t err = esp_partition_write(running_partition, 0, &zero_byte, 1);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write 0x00 to the first byte of the running partition");
    } else {
        ESP_LOGI(TAG, "Restarting system to boot from test partition");

        return (void)releaseHeapObjectsAndReboot();
    }
}

bool attachPartition(const String &_from, String _to) {
    size_t offset = 0;
    uint8_t bytes[16];
    uint8_t buff[bufSize]; // on-demand copy buffer (was a resident global, see docs/milestone_2.md)
    launcherConsolePrintf("From: %s\nTo: %s\n", _from.c_str(), _to.c_str());
    File to = SDM.open(_to, FILE_READ);
    if (!to) {
        displayError("Can't open target");
        return false;
    }

    // look fot FAT/SPIFFS/LittleFS partition offset
    if (to.seek(0x8000) && to.read(bytes, 16) == 16 && bytes[0] == 0xAA && bytes[1] == 0x50 &&
        bytes[2] == 0x01) {
        for (int i = 0x0; i <= 0x1A0; i += 0x20) {
            if (!to.seek(0x8000 + i)) {
                launcherConsolePrintln("Error: Could not move cursor to read partition info");
                to.close();
                return false;
            }
            to.read(bytes, 16);

            if (bytes[3] == 0x81 || bytes[3] == 0x82 || bytes[3] == 0x83) {
                // Offset is a uint32 at bytes 4-7, little-endian (ESP-IDF partition table format)
                offset = (uint32_t)bytes[0x04] | ((uint32_t)bytes[0x05] << 8) |
                         ((uint32_t)bytes[0x06] << 16) | ((uint32_t)bytes[0x07] << 24);
                launcherConsolePrintf("offset=%d\n", offset);
            }
        }
    }
    to.close();

    if (!offset) {
        const uint32_t table_offset = 0x8000;
        const uint32_t app_offset = 0x10000;

        File original = SDM.open(_to, FILE_READ);
        if (!original) {
            displayError("Can't open target");
            return false;
        }
        const uint32_t app_size = (original.size() + 0xFFFFu) & ~0xFFFFu;
        const uint32_t spiffs_offset = app_offset + app_size;

        uint32_t spiffs_size = 64 * 1024;
        {
            File fromFile = SDM.open(_from, FILE_READ);
            if (fromFile) {
                spiffs_size = (fromFile.size() + 0xFFFu) & ~0xFFFu;
                fromFile.close();
            }
        }

        int dot_pos = _to.lastIndexOf('.');
        int suffix = 1;
        String new_path = _to.substring(0, dot_pos) + "_with_DATA" + _to.substring(dot_pos);
        while (SDM.exists(new_path)) {
            new_path = _to.substring(0, dot_pos) + "_with_DATA_" + String(suffix) + _to.substring(dot_pos);
            suffix++;
        }

        File rebuilt = SDM.open(new_path, FILE_WRITE, true);
        if (!rebuilt) {
            displayError("Can't create target");
            original.close();
            return false;
        }

        memset(buff, 0xFF, sizeof(buff));
        rebuilt.seek(0);
        size_t remaining = app_offset;
        while (remaining > 0) {
            size_t w = min((size_t)sizeof(buff), remaining);
            rebuilt.write(buff, w);
            remaining -= w;
        }

        uint8_t *table = (uint8_t *)heap_caps_malloc(PARTITION_SIZE, MALLOC_CAP_INTERNAL);
        if (table == NULL) {
            displayError("No memory");
            rebuilt.close();
            original.close();
            return false;
        }

        memset(table, 0xFF, PARTITION_SIZE);

        uint8_t *entry = table;
        entry[0] = 0xAA;
        entry[1] = 0x50;
        entry[2] = 0x00;
        entry[3] = 0x00;
        entry[4] = app_offset & 0xFF;
        entry[5] = (app_offset >> 8) & 0xFF;
        entry[6] = (app_offset >> 16) & 0xFF;
        entry[7] = (app_offset >> 24) & 0xFF;
        entry[8] = app_size & 0xFF;
        entry[9] = (app_size >> 8) & 0xFF;
        entry[10] = (app_size >> 16) & 0xFF;
        entry[11] = (app_size >> 24) & 0xFF;
        memset(entry + 12, 0, 16);
        memcpy(entry + 12, "factory", 7);
        memset(entry + 28, 0, 4);

        entry = table + 0x20;
        entry[0] = 0xAA;
        entry[1] = 0x50;
        entry[2] = 0x01;
        entry[3] = 0x82;
        entry[4] = spiffs_offset & 0xFF;
        entry[5] = (spiffs_offset >> 8) & 0xFF;
        entry[6] = (spiffs_offset >> 16) & 0xFF;
        entry[7] = (spiffs_offset >> 24) & 0xFF;
        entry[8] = spiffs_size & 0xFF;
        entry[9] = (spiffs_size >> 8) & 0xFF;
        entry[10] = (spiffs_size >> 16) & 0xFF;
        entry[11] = (spiffs_size >> 24) & 0xFF;
        memset(entry + 12, 0, 16);
        memcpy(entry + 12, "spiffs", 6);
        memset(entry + 28, 0, 4);

        table[0x40] = 0xEB;
        table[0x41] = 0xEB;

        rebuilt.seek(table_offset);
        rebuilt.write(table, PARTITION_SIZE);
        heap_caps_free(table);

        rebuilt.seek(app_offset);
        size_t app_remaining = min((size_t)original.size(), (size_t)app_size);
        while (app_remaining > 0) {
            size_t r = original.read(buff, min((size_t)sizeof(buff), app_remaining));
            if (!r) break;
            rebuilt.write(buff, r);
            app_remaining -= r;
        }

        rebuilt.close();
        original.close();

        _to = new_path;
        offset = spiffs_offset;
    }

    File target = SDM.open(_to, FILE_WRITE);
    if (!target) {
        displayError("Can't reopen target");
        return false;
    }

    // Adjust the target file size
    if (target.size() > offset) {
        // Erases the old partition with 0xFF
        target.seek(offset);
        while (target.position() < target.size()) {
            int angle = (360 * (target.position() - offset)) / (target.size() - offset);
            tft->drawArc(tftWidth / 2, tftHeight / 2, 50, 45, 0, angle, FGCOLOR);
            target.write(0xFF);
        }
    } else if (target.size() < offset) {
        // fill with 0xFF until offset
        target.seek(target.size());
        size_t fillLen = offset - target.size();
        const size_t chunk = 512;
        uint8_t buf[chunk];
        memset(buf, 0xFF, chunk);
        while (fillLen > 0) {
            int angle = (360 * (offset - fillLen)) / offset;
            tft->drawArc(tftWidth / 2, tftHeight / 2, 40, 35, 0, angle, FGCOLOR);
            size_t w = min(chunk, fillLen);
            target.write(buf, w);
            fillLen -= w;
        }
    }

    // copy data from source file
    File from = SDM.open(_from, FILE_READ);
    if (!from) {
        displayError("Can't open source");
        target.close();
        return false;
    }

    target.seek(offset);
    while (true) {
        int angle = (360 * (target.position() - offset)) / from.size();
        tft->drawArc(tftWidth / 2, tftHeight / 2, 35, 30, 0, angle, ALCOLOR);
        size_t bytesRead = from.read(buff, sizeof(buff));
        if (!bytesRead) break;
        target.write(buff, bytesRead);
    }

    from.close();
    target.close();

    launcherConsolePrintf(
        "Partition data from '%s' attached at offset 0x%X into '%s'\n",
        _from.c_str(),
        (unsigned int)offset,
        _to.c_str()
    );

    return true;
}
