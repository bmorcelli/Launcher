#include "partition_install_layout.h"

#include "display.h"

#include <algorithm>

bool launcherPrepareInstallDataPartitions(
    LauncherPartitionTable &table, bool spiffs, uint32_t spiffsSize, LauncherPartitionEntry &spiffsEntry,
    bool &hasSpiffsEntry, std::vector<LauncherInstallFatPartition> &fatPartitions, String &error
) {
    hasSpiffsEntry = false;
    for (LauncherInstallFatPartition &fatPartition : fatPartitions) {
        fatPartition.entry = LauncherPartitionEntry();
        fatPartition.hasEntry = false;
    }

    auto prepareSpiffs = [&]() {
        LauncherPartitionEntry *existing = launcherPartitionFindByLabel(table, "spiffs");
        if (existing) {
            if (!existing->isData() || existing->subtype != 0x82) {
                error = "Partition spiffs is incompatible";
                return false;
            }
            if (spiffsSize == LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE) {
                uint32_t oldOffset = existing->offset;
                if (!launcherPartitionRemoveEntryByOffset(table, oldOffset)) {
                    error = "Could not resize spiffs partition";
                    return false;
                }
                if (!launcherPartitionCreateDataInLargestFreeRange(table, 0x82, "spiffs", spiffsEntry, error))
                    return false;
                return true;
            }
            if (spiffsSize != LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE && existing->size < spiffsSize) {
                error = "Partition spiffs is too small or incompatible";
                return false;
            }
            spiffsEntry = *existing;
        } else if (spiffsSize == LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE) {
            if (!launcherPartitionCreateDataInLargestFreeRange(table, 0x82, "spiffs", spiffsEntry, error))
                return false;
        } else {
            if (!launcherPartitionFindOrCreateData(table, 0x82, "spiffs", spiffsSize, spiffsEntry, error))
                return false;
        }
        hasSpiffsEntry = true;
        return true;
    };

    if (spiffs && spiffsSize > 0 && spiffsSize != LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE) {
        if (!prepareSpiffs()) return false;
    }

    for (size_t i = 0; i < fatPartitions.size(); ++i) {
        LauncherInstallFatPartition &fatPartition = fatPartitions[i];
        if (fatPartition.partitionSize == 0) continue;
        if (fatPartition.label.isEmpty()) fatPartition.label = i == 0 ? "sys" : "vfs";
        const char *label = fatPartition.label.c_str();
        uint32_t desiredSize = launcherPartitionDefaultFatSize(label);
        if (desiredSize < fatPartition.partitionSize) desiredSize = fatPartition.partitionSize;
        if (!launcherPartitionFindOrCreateData(table, 0x81, label, desiredSize, fatPartition.entry, error))
            return false;
        fatPartition.hasEntry = true;
    }

    if (spiffs && spiffsSize == LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE) {
        if (!prepareSpiffs()) return false;
    }
    return true;
}

bool launcherSelectInstallLayout(
    LauncherPartitionTable &table, size_t updateSize, const String &defaultLabel, bool spiffs,
    uint32_t spiffsSize, std::vector<LauncherInstallFatPartition> &fatPartitions,
    LauncherPartitionEntry &appEntry, LauncherPartitionEntry &spiffsEntry, bool &hasSpiffsEntry, String &error
) {
    const uint32_t requiredAppPartitionSize =
        launcherAlignUp(static_cast<uint32_t>(updateSize), LAUNCHER_APP_PARTITION_ALIGNMENT);
    uint32_t requiredInstallSize = requiredAppPartitionSize;
    for (const LauncherInstallFatPartition &fatPartition : fatPartitions) {
        requiredInstallSize += fatPartition.partitionSize;
    }
    if (spiffs && spiffsSize > 0 && spiffsSize != LAUNCHER_INSTALL_USE_REMAINING_SPIFFS_SIZE) {
        requiredInstallSize += spiffsSize;
    }
    std::vector<LauncherPartitionEntry> originalApps;
    for (const LauncherPartitionEntry &entry : table.entries) {
        if (launcherPartitionIsReplaceableApp(entry)) originalApps.push_back(entry);
    }

    LauncherPartitionTable directCandidate = table;
    LauncherPartitionEntry directApp;
    LauncherPartitionEntry directSpiffs;
    std::vector<LauncherInstallFatPartition> directFats = fatPartitions;
    bool directHasSpiffs = false;
    if (launcherPartitionCreateOtaApp(
            directCandidate, updateSize, defaultLabel.c_str(), &directApp, &error
        ) &&
        launcherPrepareInstallDataPartitions(
            directCandidate, spiffs, spiffsSize, directSpiffs, directHasSpiffs, directFats, error
        ) &&
        launcherPartitionValidate(directCandidate, &error)) {
        table = directCandidate;
        appEntry = directApp;
        spiffsEntry = directSpiffs;
        hasSpiffsEntry = directHasSpiffs;
        fatPartitions = directFats;
        return true;
    }

    directCandidate = table;
    directApp = LauncherPartitionEntry();
    directSpiffs = LauncherPartitionEntry();
    directFats = fatPartitions;
    directHasSpiffs = false;
    if (launcherPrepareInstallDataPartitions(
            directCandidate, spiffs, spiffsSize, directSpiffs, directHasSpiffs, directFats, error
        ) &&
        launcherPartitionCreateOtaApp(
            directCandidate, updateSize, defaultLabel.c_str(), &directApp, &error
        ) &&
        launcherPartitionValidate(directCandidate, &error)) {
        table = directCandidate;
        appEntry = directApp;
        spiffsEntry = directSpiffs;
        hasSpiffsEntry = directHasSpiffs;
        fatPartitions = directFats;
        return true;
    }

    if (originalApps.empty() &&
        std::any_of(table.entries.begin(), table.entries.end(), launcherPartitionIsRemovableInstallData)) {
        for (int removalPass = 0; removalPass < 2; ++removalPass) {
            const bool removeSpiffs = removalPass == 1;
            directCandidate = table;
            directApp = LauncherPartitionEntry();
            directSpiffs = LauncherPartitionEntry();
            directFats = fatPartitions;
            directHasSpiffs = false;

            if (!launcherPartitionRemoveInstallDataPartitions(directCandidate, removeSpiffs)) continue;
            if (launcherPartitionCreateOtaApp(
                    directCandidate, updateSize, defaultLabel.c_str(), &directApp, &error
                ) &&
                launcherPrepareInstallDataPartitions(
                    directCandidate, spiffs, spiffsSize, directSpiffs, directHasSpiffs, directFats, error
                ) &&
                launcherPartitionValidate(directCandidate, &error)) {
                table = directCandidate;
                appEntry = directApp;
                spiffsEntry = directSpiffs;
                hasSpiffsEntry = directHasSpiffs;
                fatPartitions = directFats;
                return true;
            }
        }
    }

    LauncherPartitionTable original = table;
    std::vector<Option> choices;
    std::vector<String> choiceLabels;
    choices.push_back({String("Need ") + launcherSizeLabel(requiredInstallSize), []() {}});
    choiceLabels.push_back(choices.back().label);

    auto addChoice = [&](const String &label,
                         const LauncherPartitionTable &candidate,
                         const LauncherPartitionEntry &candidateApp,
                         const LauncherPartitionEntry &candidateSpiffs,
                         bool candidateHasSpiffs,
                         const std::vector<LauncherInstallFatPartition> &candidateFats) {
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
              &fatPartitions,
              candidate,
              candidateApp,
              candidateSpiffs,
              candidateHasSpiffs,
              candidateFats]() mutable {
                 table = candidate;
                 appEntry = candidateApp;
                 spiffsEntry = candidateSpiffs;
                 hasSpiffsEntry = candidateHasSpiffs;
                 fatPartitions = candidateFats;
             }}
        );
    };

    auto addAutoLayoutChoice = [&](const String &label, LauncherPartitionTable candidate) {
        LauncherPartitionEntry candidateApp;
        LauncherPartitionEntry candidateSpiffs;
        std::vector<LauncherInstallFatPartition> candidateFats = fatPartitions;
        bool candidateHasSpiffs = false;

        if (!launcherPartitionCreateOtaApp(
                candidate, updateSize, defaultLabel.c_str(), &candidateApp, &error
            )) {
            return;
        }
        if (!launcherPrepareInstallDataPartitions(
                candidate, spiffs, spiffsSize, candidateSpiffs, candidateHasSpiffs, candidateFats, error
            ) ||
            !launcherPartitionValidate(candidate, &error)) {
            return;
        }

        addChoice(label, candidate, candidateApp, candidateSpiffs, candidateHasSpiffs, candidateFats);
    };

    for (const LauncherPartitionEntry &entry : original.entries) {
        if (!launcherPartitionIsReplaceableApp(entry) || entry.size < requiredAppPartitionSize) continue;
        LauncherPartitionTable candidate = original;
        if (!launcherPartitionRenameEntryByOffset(candidate, entry.offset, defaultLabel)) continue;
        LauncherPartitionEntry candidateApp = entry;
        memset(candidateApp.label, 0, sizeof(candidateApp.label));
        strncpy(candidateApp.label, defaultLabel.c_str(), 15);
        LauncherPartitionEntry candidateSpiffs;
        std::vector<LauncherInstallFatPartition> candidateFats = fatPartitions;
        bool candidateHasSpiffs = false;
        if (launcherPrepareInstallDataPartitions(
                candidate, spiffs, spiffsSize, candidateSpiffs, candidateHasSpiffs, candidateFats, error
            ) &&
            launcherPartitionValidate(candidate, &error)) {
            addChoice(
                String("Use ") + entry.label + " partition",
                candidate,
                candidateApp,
                candidateSpiffs,
                candidateHasSpiffs,
                candidateFats
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

        LauncherPartitionEntry candidateSpiffs;
        std::vector<LauncherInstallFatPartition> candidateFats = fatPartitions;
        bool candidateHasSpiffs = false;
        if (launcherPrepareInstallDataPartitions(
                candidate, spiffs, spiffsSize, candidateSpiffs, candidateHasSpiffs, candidateFats, error
            ) &&
            launcherPartitionValidate(candidate, &error)) {
            addChoice(
                String("Repartition ") + apps[start].label + " + free",
                candidate,
                candidateApp,
                candidateSpiffs,
                candidateHasSpiffs,
                candidateFats
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

            LauncherPartitionEntry candidateSpiffs;
            std::vector<LauncherInstallFatPartition> candidateFats = fatPartitions;
            bool candidateHasSpiffs = false;
            if (!launcherPrepareInstallDataPartitions(
                    candidate, spiffs, spiffsSize, candidateSpiffs, candidateHasSpiffs, candidateFats, error
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
                candidateFats
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

            LauncherPartitionEntry candidateSpiffs;
            std::vector<LauncherInstallFatPartition> candidateFats = fatPartitions;
            bool candidateHasSpiffs = false;
            if (!launcherPrepareInstallDataPartitions(
                    candidate, spiffs, spiffsSize, candidateSpiffs, candidateHasSpiffs, candidateFats, error
                ) ||
                !launcherPartitionValidate(candidate, &error)) {
                continue;
            }

            String label = String("Repartition ") + apps[start].label + "-" + apps[end].label;
            addChoice(label, candidate, candidateApp, candidateSpiffs, candidateHasSpiffs, candidateFats);
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

                    LauncherPartitionEntry candidateSpiffs;
                    std::vector<LauncherInstallFatPartition> candidateFats = fatPartitions;
                    bool candidateHasSpiffs = false;
                    if (!launcherPrepareInstallDataPartitions(
                            candidate,
                            spiffs,
                            spiffsSize,
                            candidateSpiffs,
                            candidateHasSpiffs,
                            candidateFats,
                            error
                        ) ||
                        !launcherPartitionValidate(candidate, &error)) {
                        continue;
                    }

                    String label = String("Remove ") + apps[start].label;
                    if (end > start) label += String("-") + apps[end].label;
                    label += removeSpiffs ? " + free + all data" : " + free + FAT data";
                    addChoice(
                        label, candidate, candidateApp, candidateSpiffs, candidateHasSpiffs, candidateFats
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
