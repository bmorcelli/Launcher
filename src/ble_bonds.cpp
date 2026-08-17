#include "ble_bonds.h"
#include "idf/launcher_platform.h"
#include "nvs_helpers.h"
#include "partition_table_model.h"
#include <cstring>
#include <vector>

namespace {
// Hard-coded in esp-nimble's ble_store_config_nvs.c, so every firmware shares it.
constexpr const char *kLiveNs = "nimble_bond";
// Launcher-owned stash, holding the records of the apps that are not running.
constexpr const char *kStoreNs = "l_bonds";
constexpr const char *kOwnerKey = "owner";
// Single-digit slots keep the "<slot>_" stash prefix at 2 chars, which leaves room
// for NimBLE's longest key ("p_dev_rec_10", 12 chars) inside the 15-char NVS limit.
constexpr int kMaxSlots = 8;
constexpr size_t kMaxKeyLen = NVS_KEY_NAME_MAX_SIZE - 1;
// A bond record is under 200 bytes in every NimBLE build; refuse anything larger
// rather than move flash around for something that is not ours.
constexpr size_t kMaxBlobSize = 512;
// Partition labels are at most 16 chars.
constexpr size_t kMaxLabelLen = 16;

// One format string for every failure, so each call site only costs a short tag.
bool fail(const char *what, int code) {
    launcherConsolePrintf("BLE bonds: %s failed (%d)\n", what, code);
    return false;
}

std::vector<String> listBlobs(const char *ns) { return lnvs::keys(ns, NVS_TYPE_BLOB); }

void slotKey(int slot, char *out) {
    out[0] = 'o';
    out[1] = static_cast<char>('0' + slot);
    out[2] = '\0';
}

bool isStashOfSlot(const String &key, int slot) {
    return key.length() > 2 && key[0] == static_cast<char>('0' + slot) && key[1] == '_';
}

String readSlotLabel(nvs_handle_t store, int slot) {
    char key[3];
    slotKey(slot, key);
    return lnvs::getString(store, key, kMaxLabelLen);
}

// Slots are keyed by label, not by OTA subtype: subtypes are renumbered when an app
// is deleted, which would hand one app's records to another -- the very crash this
// module exists to prevent.
int findSlot(nvs_handle_t store, const char *label, bool create) {
    int freeSlot = -1;
    for (int slot = 0; slot < kMaxSlots; ++slot) {
        String stored = readSlotLabel(store, slot);
        if (stored.isEmpty()) {
            if (freeSlot < 0) freeSlot = slot;
        } else if (stored == label) {
            return slot;
        }
    }
    if (!create || freeSlot < 0) return -1;

    char key[3];
    slotKey(freeSlot, key);
    if (nvs_set_str(store, key, label) != ESP_OK) return -1;
    return freeSlot;
}

// Drops every record of a slot plus its label, so the slot is free for reuse.
void clearSlot(nvs_handle_t store, int slot, const std::vector<String> &stored) {
    for (const String &key : stored) {
        if (isStashOfSlot(key, slot)) nvs_erase_key(store, key.c_str());
    }
    char key[3];
    slotKey(slot, key);
    nvs_erase_key(store, key);
}

// Deleting an app leaves its stash behind, so garbage-collect by label.
void pruneDeletedApps(nvs_handle_t store, const std::vector<String> &stored) {
    LauncherPartitionTable table;
    if (!launcherPartitionReadCurrent(table)) return;

    for (int slot = 0; slot < kMaxSlots; ++slot) {
        String label = readSlotLabel(store, slot);
        if (label.isEmpty()) continue;
        const LauncherPartitionEntry *entry = launcherPartitionFindByLabel(table, label.c_str());
        if (entry && entry->isOtaApp()) continue;
        clearSlot(store, slot, stored);
    }
}

// Bytes in, bytes out: the record is never interpreted here, so a foreign layout
// cannot hurt the Launcher the way it hurts the firmware that owns the struct.
bool copyBlob(nvs_handle_t from, const char *fromKey, nvs_handle_t to, const char *toKey) {
    return lnvs::copyBlob(from, fromKey, to, toKey, kMaxBlobSize);
}
} // namespace

bool launcherBleBondsSwitchTo(const char *label) {
    if (!label || !label[0]) return false;

    lnvs::Handle store(kStoreNs, true);
    if (!store) return false;

    String owner = lnvs::getString(store.raw(), kOwnerKey, kMaxLabelLen);
    if (owner == label) return true; // already its own store, nothing to swap

    std::vector<String> live = listBlobs(kLiveNs);
    std::vector<String> stored = listBlobs(kStoreNs);
    if (!stored.empty()) {
        pruneDeletedApps(store.raw(), stored);
        stored = listBlobs(kStoreNs); // pruning may have removed entries
    }

    int targetSlot = findSlot(store.raw(), label, false);
    bool hasStash = false;
    if (targetSlot >= 0) {
        for (const String &key : stored) {
            if (isStashOfSlot(key, targetSlot)) {
                hasStash = true;
                break;
            }
        }
    }

    // Opened only when there is something to move: opening for writing would create
    // the bond namespace on devices that never paired anything.
    lnvs::Handle live_ns;
    if ((!live.empty() || hasStash) && !live_ns.open(kLiveNs, true)) return false;

    launcherConsolePrintf(
        "BLE bonds: %s(%u) -> %s\n", owner.isEmpty() ? "?" : owner.c_str(), (unsigned)live.size(), label
    );

    bool ok = true;
    if (!live.empty()) {
        // An unknown owner means the records predate this Launcher or were written
        // outside it; they have to go, since handing a foreign layout to the app
        // about to boot is exactly what bootloops it.
        int ownerSlot = owner.isEmpty() ? -1 : findSlot(store.raw(), owner.c_str(), true);
        if (!owner.isEmpty() && ownerSlot < 0) ok = fail("stash", ESP_ERR_NVS_NOT_ENOUGH_SPACE);
        for (const String &key : live) {
            if (ownerSlot < 0) break;
            if (key.length() + 2 > kMaxKeyLen) {
                ok = fail("stash", key.length());
                continue;
            }
            String stashKey = String(ownerSlot) + "_" + key;
            if (!copyBlob(live_ns.raw(), key.c_str(), store.raw(), stashKey.c_str())) ok = false;
        }
        if (nvs_erase_all(live_ns.raw()) != ESP_OK) ok = false;
    }

    if (targetSlot >= 0) {
        for (const String &key : stored) {
            if (!isStashOfSlot(key, targetSlot)) continue;
            if (!copyBlob(store.raw(), key.c_str(), live_ns.raw(), key.c_str() + 2)) ok = false;
        }
        // Free the slot either way: the records are live now, and a partial restore
        // must not leave half of a bond behind to be handed back on the next switch.
        clearSlot(store.raw(), targetSlot, stored);
    }

    if (!lnvs::setString(store.raw(), kOwnerKey, label)) ok = false;
    if (!store.commit()) ok = false;
    if (live_ns && !live_ns.commit()) ok = false;

    if (!ok) launcherConsolePrintf("BLE bonds: switch incomplete, re-pairing may be needed\n");
    return ok;
}

bool launcherBleBondsEraseAll() {
    bool live = lnvs::eraseNamespace(kLiveNs);
    bool stash = lnvs::eraseNamespace(kStoreNs);
    return live && stash;
}

size_t launcherBleBondsCount() { return listBlobs(kLiveNs).size() + listBlobs(kStoreNs).size(); }
