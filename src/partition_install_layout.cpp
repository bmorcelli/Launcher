#include "partition_install_layout.h"

#include "display.h"

#include <algorithm>

bool launcherPrepareInstallDataPartitions(
    LauncherPartitionTable &table, std::vector<LauncherInstallDataPartition> &dataPartitions, String &error
) {
    for (auto &dp : dataPartitions) {
        dp.entry = LauncherPartitionEntry();
        dp.hasEntry = false;
    }

    // Fixed-size data partitions (SPIFFS/LittleFS) first so FAT finds accurate free space
    for (auto &dp : dataPartitions) {
        if (dp.subtype == 0x81) continue;
        if (dp.partitionSize == 0 || dp.partitionSize == LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE) continue;
        const uint8_t stype = dp.subtype;
        LauncherPartitionEntry *existing = launcherPartitionFindByLabel(table, dp.label.c_str());
        if (existing) {
            if (!existing->isData() || existing->subtype != stype) {
                error = String("Partition ") + dp.label + " is incompatible";
                return false;
            }
            dp.entry = *existing;
        } else {
            if (!launcherPartitionFindOrCreateData(
                    table, stype, dp.label.c_str(), dp.partitionSize, dp.entry, error
                ))
                return false;
        }
        dp.hasEntry = true;
    }

    // FAT partitions
    for (auto &dp : dataPartitions) {
        if (dp.subtype != 0x81) continue;
        if (dp.partitionSize == 0) continue;
        const char *label = dp.label.c_str();
        const bool usePayloadSize = strcmp(label, "sys") == 0 || strcmp(label, "system") == 0;
        uint32_t desiredSize = usePayloadSize ? dp.partitionSize : launcherPartitionDefaultFatSize(label);
        if (!launcherPartitionFindOrCreateData(table, 0x81, label, desiredSize, dp.entry, error))
            return false;
        dp.hasEntry = true;
    }

    // USE_REMAINING data partitions last (largest available free space)
    for (auto &dp : dataPartitions) {
        if (dp.subtype == 0x81) continue;
        if (dp.partitionSize != LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE) continue;
        const uint8_t stype = dp.subtype;
        LauncherPartitionEntry *existing = launcherPartitionFindByLabel(table, dp.label.c_str());
        if (existing) {
            if (!existing->isData() || existing->subtype != stype) {
                error = String("Partition ") + dp.label + " is incompatible";
                return false;
            }
            uint32_t oldOffset = existing->offset;
            if (!launcherPartitionRemoveEntryByOffset(table, oldOffset)) {
                error = String("Could not resize ") + dp.label + " partition";
                return false;
            }
        }
        if (!launcherPartitionCreateDataInLargestFreeRange(table, stype, dp.label.c_str(), dp.entry, error))
            return false;
        dp.hasEntry = true;
    }

    return true;
}

String launcherInstallUpdateTarget(
    const String &displayName, size_t updateSize,
    const std::vector<LauncherInstallDataPartition> &dataPartitions
) {
    LauncherPartitionTable table;
    if (!launcherPartitionReadCurrent(table, nullptr)) return "";
    String label = launcherPartitionSanitizedAppLabelBase(displayName);
    std::vector<LauncherInstallDataPartition> parts = dataPartitions;
    LauncherPartitionEntry appEntry;
    String error;
    if (!launcherSelectUpdateLayout(table, updateSize, label, parts, appEntry, error)) return "";
    return label;
}

// The partition `entry` was before a layout change (same type + label), or nullptr if new.
static const LauncherPartitionEntry *
updateLayoutSource(const LauncherPartitionTable &current, const LauncherPartitionEntry &entry) {
    const LauncherPartitionEntry *source = launcherPartitionFindByLabel(current, entry.label);
    return source && source->type == entry.type ? source : nullptr;
}

// Only other firmwares and their FAT/SPIFFS/LittleFS data may be relocated: never the
// running Launcher, NVS, otadata or other system data the IDF keeps using until reboot.
static bool updateLayoutMovesOnlyMovable(
    const LauncherPartitionTable &current, const LauncherPartitionTable &target, String &error
) {
    for (const LauncherPartitionEntry &entry : target.entries) {
        const LauncherPartitionEntry *source = updateLayoutSource(current, entry);
        if (!source || source->offset == entry.offset) continue;
        const bool movable = entry.isData()
                                 ? entry.subtype == 0x81 || entry.subtype == 0x82 || entry.subtype == 0x83
                                 : launcherPartitionIsReplaceableApp(*source);
        if (!movable) {
            error = String("Can't move ") + entry.label;
            return false;
        }
    }
    return true;
}

bool launcherSelectUpdateLayout(
    LauncherPartitionTable &table, size_t updateSize, const String &label,
    std::vector<LauncherInstallDataPartition> &dataPartitions, LauncherPartitionEntry &appEntry, String &error
) {
    const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, label.c_str());
    if (!entry || !launcherPartitionIsReplaceableApp(*entry)) {
        error = label + " can't be updated";
        return false;
    }

    LauncherPartitionTable candidate = table;
    std::vector<LauncherInstallDataPartition> parts = dataPartitions;
    LauncherPartitionEntry candidateApp = *entry;
    bool ok;
    if (entry->size >= updateSize) {
        ok = launcherPrepareInstallDataPartitions(candidate, parts, error) &&
             launcherPartitionValidate(candidate, &error);
    } else {
        // Grow: drop the old slot, then try free space as-is before compacting the flash.
        launcherPartitionRemoveEntryByOffset(candidate, entry->offset);
        LauncherPartitionTable base = candidate;
        ok = launcherPartitionCreateOtaApp(candidate, updateSize, label.c_str(), &candidateApp, &error) &&
             launcherPrepareInstallDataPartitions(candidate, parts, error) &&
             launcherPartitionValidate(candidate, &error);
        if (!ok) {
            candidate = base;
            parts = dataPartitions;
            ok = launcherPartitionCompact(candidate, &error) &&
                 updateLayoutMovesOnlyMovable(table, candidate, error) &&
                 launcherPartitionCreateOtaApp(candidate, updateSize, label.c_str(), &candidateApp, &error) &&
                 launcherPrepareInstallDataPartitions(candidate, parts, error) &&
                 launcherPartitionValidate(candidate, &error);
        }
    }
    if (!ok) return false;
    table = candidate;
    dataPartitions = parts;
    appEntry = candidateApp;
    return true;
}

bool launcherApplyUpdateLayoutMoves(
    const LauncherPartitionTable &target, const LauncherPartitionEntry &appEntry, String &error
) {
    LauncherPartitionTable current; // still the table on flash: nothing was written yet
    if (!launcherPartitionReadCurrent(current, &error)) return false;
    // Copy only partitions compacted toward the start: the app is flashed fresh, and data
    // re-created further up (largest free range) is rewritten or starts empty, as on install.
    LauncherPartitionTable moves = target;
    bool anyMove = false;
    moves.entries.clear();
    for (const LauncherPartitionEntry &entry : target.entries) {
        const LauncherPartitionEntry *source = updateLayoutSource(current, entry);
        if (entry.offset == appEntry.offset) continue;
        if (source && source->offset < entry.offset) continue;
        if (source && source->offset > entry.offset) anyMove = true;
        moves.entries.push_back(entry);
    }
    if (!anyMove) return true;

    displayRedStripe("Optimizing flash");
    if (!launcherPartitionMigrateMovedData(current, moves, &error)) return false;
    displayRedStripe("Writing table");
    return launcherPartitionWriteGeneratedTable(target, &error);
}

bool launcherSelectInstallLayout(
    LauncherPartitionTable &table, size_t updateSize, const String &defaultLabel,
    std::vector<LauncherInstallDataPartition> &dataPartitions, LauncherPartitionEntry &appEntry, String &error
) {
    const uint32_t requiredAppPartitionSize =
        launcherAlignUp(static_cast<uint32_t>(updateSize), LAUNCHER_APP_PARTITION_ALIGNMENT);
    uint32_t requiredInstallSize = requiredAppPartitionSize;
    for (const auto &dp : dataPartitions) {
        if (dp.partitionSize > 0 && dp.partitionSize != LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE)
            requiredInstallSize += dp.partitionSize;
    }
    std::vector<LauncherPartitionEntry> originalApps;
    for (const LauncherPartitionEntry &entry : table.entries) {
        if (launcherPartitionIsReplaceableApp(entry)) originalApps.push_back(entry);
    }

    LauncherPartitionTable directCandidate = table;
    LauncherPartitionEntry directApp;
    std::vector<LauncherInstallDataPartition> directPartitions = dataPartitions;
    if (launcherPartitionCreateOtaApp(
            directCandidate, updateSize, defaultLabel.c_str(), &directApp, &error
        ) &&
        launcherPrepareInstallDataPartitions(directCandidate, directPartitions, error) &&
        launcherPartitionValidate(directCandidate, &error)) {
        table = directCandidate;
        appEntry = directApp;
        dataPartitions = directPartitions;
        return true;
    }

    directCandidate = table;
    directApp = LauncherPartitionEntry();
    directPartitions = dataPartitions;
    if (launcherPrepareInstallDataPartitions(directCandidate, directPartitions, error) &&
        launcherPartitionCreateOtaApp(
            directCandidate, updateSize, defaultLabel.c_str(), &directApp, &error
        ) &&
        launcherPartitionValidate(directCandidate, &error)) {
        table = directCandidate;
        appEntry = directApp;
        dataPartitions = directPartitions;
        return true;
    }

    if (originalApps.empty() &&
        std::any_of(table.entries.begin(), table.entries.end(), launcherPartitionIsRemovableInstallData)) {
        for (int removalPass = 0; removalPass < 2; ++removalPass) {
            const bool removeSpiffs = removalPass == 1;
            directCandidate = table;
            directApp = LauncherPartitionEntry();
            directPartitions = dataPartitions;

            if (!launcherPartitionRemoveInstallDataPartitions(directCandidate, removeSpiffs)) continue;
            if (launcherPartitionCreateOtaApp(
                    directCandidate, updateSize, defaultLabel.c_str(), &directApp, &error
                ) &&
                launcherPrepareInstallDataPartitions(directCandidate, directPartitions, error) &&
                launcherPartitionValidate(directCandidate, &error)) {
                table = directCandidate;
                appEntry = directApp;
                dataPartitions = directPartitions;
                return true;
            }
        }
    }

    LauncherPartitionTable original = table;
    std::vector<Option> choices;
    std::vector<String> choiceLabels;
    choices.push_back(
        {String("Need ") + launcherHumanSize(requiredInstallSize) + launcherSizeLabel(requiredInstallSize),
         []() {}}
    );
    choiceLabels.push_back(choices.back().label);

    auto addChoice = [&](const String &label,
                         const LauncherPartitionTable &candidate,
                         const LauncherPartitionEntry &candidateApp,
                         const std::vector<LauncherInstallDataPartition> &candidatePartitions) {
        for (const String &existingLabel : choiceLabels) {
            if (existingLabel == label) return;
        }
        choiceLabels.push_back(label);
        choices.push_back(
            {label,
             [&table, &appEntry, &dataPartitions, candidate, candidateApp, candidatePartitions]() mutable {
                 table = candidate;
                 appEntry = candidateApp;
                 dataPartitions = candidatePartitions;
             }}
        );
    };

    auto addAutoLayoutChoice = [&](const String &label, LauncherPartitionTable candidate) {
        LauncherPartitionEntry candidateApp;
        std::vector<LauncherInstallDataPartition> candidatePartitions = dataPartitions;

        if (!launcherPartitionCreateOtaApp(
                candidate, updateSize, defaultLabel.c_str(), &candidateApp, &error
            )) {
            return;
        }
        if (!launcherPrepareInstallDataPartitions(candidate, candidatePartitions, error) ||
            !launcherPartitionValidate(candidate, &error)) {
            return;
        }

        addChoice(label, candidate, candidateApp, candidatePartitions);
    };

    for (const LauncherPartitionEntry &entry : original.entries) {
        if (!launcherPartitionIsReplaceableApp(entry) || entry.size < requiredAppPartitionSize) continue;
        LauncherPartitionTable candidate = original;
        if (!launcherPartitionRenameEntryByOffset(candidate, entry.offset, defaultLabel)) continue;
        LauncherPartitionEntry candidateApp = entry;
        memset(candidateApp.label, 0, sizeof(candidateApp.label));
        strncpy(candidateApp.label, defaultLabel.c_str(), 15);
        std::vector<LauncherInstallDataPartition> candidatePartitions = dataPartitions;
        if (launcherPrepareInstallDataPartitions(candidate, candidatePartitions, error) &&
            launcherPartitionValidate(candidate, &error)) {
            addChoice(
                String("Use ") + entry.label + " partition", candidate, candidateApp, candidatePartitions
            );
        }
    }

    std::vector<LauncherPartitionEntry> apps = originalApps;
    std::sort(apps.begin(), apps.end(), [](const LauncherPartitionEntry &a, const LauncherPartitionEntry &b) {
        return a.offset < b.offset;
    });

    auto isSelectedApp = [&](const LauncherPartitionEntry &entry, size_t start, size_t end) {
        for (size_t i = start; i <= end; ++i) {
            if (apps[i].offset == entry.offset) return true;
        }
        return false;
    };

    auto isRemovableDataForPass = [](const LauncherPartitionEntry &entry, bool removeSpiffs) {
        if (!launcherPartitionIsRemovableInstallData(entry)) return false;
        return removeSpiffs || strcmp(entry.label, "spiffs") != 0;
    };

    auto rangeCanBeCleared = [&](size_t start, size_t end, bool removeSpiffs) {
        const uint32_t rangeStart = apps[start].offset;
        const uint32_t rangeEnd = apps[end].offset + apps[end].size;
        for (const LauncherPartitionEntry &entry : original.entries) {
            const uint32_t entryEnd = entry.offset + entry.size;
            if (entryEnd <= rangeStart || entry.offset >= rangeEnd) continue;
            if (isSelectedApp(entry, start, end)) continue;
            if (isRemovableDataForPass(entry, removeSpiffs)) continue;
            return false;
        }
        return true;
    };

    for (size_t start = 0; start < apps.size(); ++start) {
        if (apps[start].size >= requiredAppPartitionSize) continue;
        LauncherPartitionTable candidate = original;
        launcherPartitionRemoveEntryByOffset(candidate, apps[start].offset);

        LauncherPartitionEntry candidateApp;
        if (!launcherPartitionAddManualAppEntry(
                candidate,
                apps[start].subtype,
                defaultLabel.c_str(),
                apps[start].offset,
                updateSize,
                candidateApp,
                error
            )) {
            continue;
        }

        std::vector<LauncherInstallDataPartition> candidatePartitions = dataPartitions;
        if (launcherPrepareInstallDataPartitions(candidate, candidatePartitions, error) &&
            launcherPartitionValidate(candidate, &error)) {
            addChoice(
                String("Repartition ") + apps[start].label + " + free",
                candidate,
                candidateApp,
                candidatePartitions
            );
        }
    }

    if (std::any_of(
            original.entries.begin(), original.entries.end(), launcherPartitionIsRemovableInstallData
        )) {
        for (int removalPass = 0; removalPass < 2 && choices.size() == 1; ++removalPass) {
            const bool removeSpiffs = removalPass == 1;
            LauncherPartitionTable candidate = original;
            if (!launcherPartitionRemoveInstallDataPartitions(candidate, removeSpiffs)) continue;

            LauncherPartitionEntry candidateApp;
            if (!launcherPartitionCreateOtaApp(
                    candidate, updateSize, defaultLabel.c_str(), &candidateApp, &error
                )) {
                continue;
            }

            std::vector<LauncherInstallDataPartition> candidatePartitions = dataPartitions;
            if (!launcherPrepareInstallDataPartitions(candidate, candidatePartitions, error) ||
                !launcherPartitionValidate(candidate, &error)) {
                continue;
            }

            addChoice(
                removeSpiffs ? "Remove data + use free" : "Remove FAT data + use free",
                candidate,
                candidateApp,
                candidatePartitions
            );
        }
    }

    if (std::any_of(
            original.entries.begin(), original.entries.end(), launcherPartitionIsRemovableInstallData
        )) {
        for (size_t start = 0; start < apps.size(); ++start) {
            for (size_t end = start; end < apps.size(); ++end) {
                for (int removalPass = 0; removalPass < 2; ++removalPass) {
                    const bool removeSpiffs = removalPass == 1;
                    if (!rangeCanBeCleared(start, end, removeSpiffs)) continue;
                    LauncherPartitionTable candidate = original;
                    for (size_t i = start; i <= end; ++i)
                        launcherPartitionRemoveEntryByOffset(candidate, apps[i].offset);
                    if (!launcherPartitionRemoveInstallDataPartitions(candidate, removeSpiffs)) continue;

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
            uint32_t usableStart = launcherAlignUp(rangeStart, LAUNCHER_APP_PARTITION_ALIGNMENT);
            if (rangeEnd <= usableStart || rangeEnd - usableStart < requiredAppPartitionSize) continue;

            LauncherPartitionTable candidate = original;
            for (size_t i = start; i <= end; ++i)
                launcherPartitionRemoveEntryByOffset(candidate, apps[i].offset);

            LauncherPartitionEntry candidateApp;
            if (!launcherPartitionAddManualAppEntry(
                    candidate,
                    apps[start].subtype,
                    defaultLabel.c_str(),
                    usableStart,
                    updateSize,
                    candidateApp,
                    error
                )) {
                continue;
            }

            std::vector<LauncherInstallDataPartition> candidatePartitions = dataPartitions;
            if (!launcherPrepareInstallDataPartitions(candidate, candidatePartitions, error) ||
                !launcherPartitionValidate(candidate, &error)) {
                continue;
            }

            String label = String("Repartition ") + apps[start].label + "-" + apps[end].label;
            addChoice(label, candidate, candidateApp, candidatePartitions);
        }
    }

    const bool needsDataRemoval = choices.size() == 1;
    if (needsDataRemoval &&
        std::any_of(
            original.entries.begin(), original.entries.end(), launcherPartitionIsRemovableInstallData
        )) {
        for (int removalPass = 0; removalPass < 2 && choices.size() == 1; ++removalPass) {
            const bool removeSpiffs = removalPass == 1;
            for (size_t start = 0; start < apps.size(); ++start) {
                uint32_t rangeStart = apps[start].offset;
                for (size_t end = start; end < apps.size(); ++end) {
                    if (!rangeCanBeCleared(start, end, removeSpiffs)) continue;
                    uint32_t rangeEnd = apps[end].offset + apps[end].size;

                    LauncherPartitionTable candidate = original;
                    for (size_t i = start; i <= end; ++i)
                        launcherPartitionRemoveEntryByOffset(candidate, apps[i].offset);
                    if (!launcherPartitionRemoveInstallDataPartitions(candidate, removeSpiffs)) continue;

                    LauncherPartitionEntry candidateApp;
                    const uint32_t usableStart =
                        launcherAlignUp(rangeStart, LAUNCHER_APP_PARTITION_ALIGNMENT);
                    if (rangeEnd <= usableStart || rangeEnd - usableStart < requiredAppPartitionSize)
                        continue;
                    if (!launcherPartitionAddManualAppEntry(
                            candidate,
                            apps[start].subtype,
                            defaultLabel.c_str(),
                            usableStart,
                            updateSize,
                            candidateApp,
                            error
                        )) {
                        continue;
                    }

                    std::vector<LauncherInstallDataPartition> candidatePartitions = dataPartitions;
                    if (!launcherPrepareInstallDataPartitions(candidate, candidatePartitions, error) ||
                        !launcherPartitionValidate(candidate, &error)) {
                        continue;
                    }

                    String label = String("Remove ") + apps[start].label;
                    if (end > start) label += String("-") + apps[end].label;
                    label += removeSpiffs ? " + free + all data" : " + free + FAT data";
                    addChoice(label, candidate, candidateApp, candidatePartitions);
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
