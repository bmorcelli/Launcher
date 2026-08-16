#ifndef LAUNCHER_BLE_BONDS_H
#define LAUNCHER_BLE_BONDS_H

#include <Arduino.h>
#include <stddef.h>

// NimBLE persists its bonds as a raw memcpy of `union ble_store_value` into the
// shared "nimble_bond" NVS namespace, with no version or length tag. The layout of
// that union depends on the NimBLE version and on build flags (BLE_STORE_MAX_BONDS,
// ENC_ADV_DATA, the rpa_rec/local_irk/csfc members added in 1.6+), so a record
// written by one firmware can be larger than the stack buffer the next firmware
// reads it into -- esp-nimble's get_nvs_db_value() reads the blob's own size into a
// stack `union ble_store_value` without checking it fits, which is the
// "Stack smashing protect failure!" bootloop seen when alternating firmwares.
//
// The Launcher can't translate between layouts, but it can make sure each app only
// ever sees the records it wrote itself: on every switch the outgoing app's records
// are moved to a Launcher-owned stash and the incoming app's own records are moved
// back. Bonds survive, nothing foreign is ever left in the live store, and the
// Launcher only copies bytes -- it never interprets them.

// Hands the live NimBLE bond store over to `label`, stashing whatever the previous
// owner left behind. A no-op when `label` already owns the live store. Returns false
// if NVS refused a step, in which case some bonds may have to be paired again.
bool launcherBleBondsSwitchTo(const char *label);

// Wipes the live bond store and every stashed snapshot. All devices must be paired
// again afterwards.
bool launcherBleBondsEraseAll();

// Stored records: the ones live for the app that owns the store plus the ones held
// in the stash for the other installed apps.
size_t launcherBleBondsCount();

#endif /* LAUNCHER_BLE_BONDS_H */
