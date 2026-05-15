# Partitioner Upgrade

## Current State

* The Launcher changes the partition scheme by writing hardcoded partition table binaries to flash.
* Existing install/update flows still rely on ESP-IDF partition APIs for the active partition table.
* This is not enough for dynamic installs because a newly written partition table is not visible to ESP-IDF partition iterators until reboot.

## Objective

Create a dynamic partitioner similar to `fdisk`, able to build, validate, write, and use a new partition table during the same install flow.

The installer must not require a reboot between changing the partition table and flashing the selected firmware. This is required for the online installer and for the custom bootloader behavior.

## Core Design

### In-memory partition model

Create a partition table model that becomes the source of truth during editing and installation.

The model must support:

* Reading the current partition table from flash.
* Parsing app, data, and custom partition entries.
* Creating, editing, removing, formatting, and listing partitions.
* Calculating free ranges.
* Generating a valid ESP partition table binary, including terminator and checksum/MD5.
* Validating the generated table before anything is written to flash.

After the new table is generated, the installer must use this in-memory model for offsets and sizes. Do not rely on `esp_partition_find_*()`, `esp_ota_get_next_update_partition()`, or similar APIs for partitions that only exist in the new table.

### Raw flash install path

When a firmware install requires a partition table change, use low-level flash APIs:

* `esp_flash_erase_region()` to erase the target region.
* `esp_flash_write()` to write firmware/data directly to the selected offset.
* `esp_flash_read()` or equivalent validation reads to verify critical bytes.

The update layer should gain a raw-offset install API, for example:

* install app to explicit offset/size.
* install SPIFFS/FAT image to explicit offset/size.
* erase/format data partition by explicit offset/size.

The current IDF update wrapper can remain for installs that use the already active partition table, but dynamic partition installs must use the raw path.

## Required Install Transaction

For dynamic online installs, the safe sequence is:

1. Parse the current partition table.
2. Build the desired new partition table in RAM.
3. Validate the full table and target ranges.
4. Select the app/data target partitions from the new in-memory table.
5. Erase and flash the firmware directly to the new app partition offset.
6. Erase and flash or prepare related FAT/SPIFFS partitions directly by offset.
7. Verify the app image header and written size.
8. Write the new partition table to flash at `0x8000`.
9. Manually update `otadata` to select the new app partition when needed.
10. Reboot.

`otadata` must be updated only after the new table and firmware are valid. This keeps the old boot target available if flashing fails before the final activation step.

## User Interface

Add an interface where users can:

* Create OTA, SPIFFS, and FAT partitions.
* Edit partition size.
* Remove partitions.
* Format partitions.
* See offsets, sizes, labels, free space, and protected status.
* See how much flash is needed when an install cannot fit.

The UI must not allow invalid layouts. It should snap sizes and offsets to valid erase/alignment boundaries and show the resulting values before applying changes.

## Protected Partitions

The partitioner must protect the running Launcher partition and required boot partitions.

Rules:

* Detect the running Launcher partition using the current boot state, not only by label.
* TEST or FACTORY Launcher partition cannot be deleted.
* TEST or FACTORY Launcher partition cannot be resized below the actual Launcher firmware size on flash.
* Bootloader, partition table, `otadata`, NVS, and required recovery partitions must not be damaged.
* Current running app partition must not be erased unless the operation is an intentional Launcher self-update with a recovery path.

## Firmware Install Rules

When installing firmware:

* Check the firmware size before modifying flash.
* If there is enough free room, create a new OTA app partition in the in-memory table and flash the firmware to that raw offset.
* If there is no room, prompt the user to delete or resize partitions and show the exact missing size.
* If replacing an existing app, preserve or update the app name metadata.
* If the firmware includes or declares FAT partitions, create them according to their names:
  * `sys`: `0x100000`
  * `vfs`: `0x70000`
  * any other FAT label: `0x80000`
* SPIFFS/FAT partitions must be erased or written by explicit raw offset when they are part of the new table.

## Boot Selection and `otadata`

After installing firmware:

* Add or update an APP entry with the firmware name.
* If the firmware replaced an existing app, update that existing APP entry.
* If there are multiple APPs, selecting one must set the correct boot target and reboot.

Because the selected partition may come from a table that was just generated, boot selection cannot depend only on `esp_ota_set_boot_partition()`.

The implementation must manually manipulate `otadata` when needed:

* Locate the `otadata` partition from the current or generated table.
* Write valid OTA select records compatible with the custom bootloader.
* If the custom bootloader follows ESP-IDF OTA data format, write the correct sequence/state/CRC fields.
* If the custom bootloader has custom rules, document them and implement that exact format.

## App Metadata

Maintain metadata for installed apps separately from the partition table.

The metadata should include:

* Display name.
* Partition label.
* Partition subtype or app slot number.
* Offset and size.
* Firmware source when available.
* Install timestamp or version when available.

Do not rely on partition labels alone as user-facing app names. Labels are short and constrained.

## Validation Requirements

Before writing anything:

* No partition may overlap another partition.
* No partition may exceed physical flash size.
* Offsets and sizes must respect erase/write alignment.
* Labels must be valid and unique where required.
* App partitions must be large enough for the firmware.
* Data partitions must be large enough for their image or requested format size.
* The generated partition table must fit in the partition table sector.
* Protected partitions must remain valid.

Before activation:

* The app image must have a valid ESP image magic byte.
* The flashed size must match the expected firmware size.
* Prefer image/hash verification when possible.
* The partition table must be written successfully.
* `otadata` must only be written after firmware and table verification.

## Failure and Recovery

The operation should be treated as a transaction with clear failure points.

Required behavior:

* If firmware flashing fails, do not write `otadata`.
* If partition table generation fails, do not write flash.
* If partition table writing fails, do not write `otadata`.
* If activation fails, keep the previous boot target.
* The bootloader should be able to fall back to TEST/FACTORY Launcher if the selected app cannot boot.
* Keep a known-good default partition generator or restore option.

For app flashing, use the same safety concept already used by the update wrapper: avoid making a partially written app look bootable. Defer writing the app magic/header until the image is completely written and validated.

## Implementation Milestones

### Milestone 1 - Parser and generator - Completed

* [x] Parse the active partition table from flash.
* [x] Build a structured in-memory partition list.
* [x] Generate a valid binary partition table.
* [x] Generate partition table MD5.
* [x] Add validation for overlaps, bounds, labels, alignment, protected partitions, `otadata`, and flash size.
* [x] Add helpers to calculate free ranges.
* [x] Add helpers to create OTA app and data partition entries in the in-memory table.

Implementation files:

* `src/partition_table_model.h`
* `src/partition_table_model.cpp`

### Milestone 2 - Read-only UI - Completed

* [x] Replace or enhance the current partition list with a full layout view.
* [x] Show partition type, subtype, label, offset, size, free ranges, and protected status.

Implementation file:

* `src/partitioner.cpp`

### Milestone 3 - Raw flash writer - Completed

* [x] Add raw install/write helpers using explicit offsets and sizes.
* [x] Support raw app flashing with deferred app header.
* [x] Support generic raw data flashing by explicit offset/size.
* [x] Add dedicated FAT/SPIFFS/data format preparation for newly created partitions.
* [x] Add full app image verification beyond magic-byte and written-size checks.

Implementation files:

* `src/idf/idf_update.h`
* `src/idf/idf_update.cpp`

### Milestone 4 - Manual editor - Completed

* [x] Add create/edit/remove/format actions.
* [x] Validate changes before applying through the UI.
* [x] Write the generated table to flash from the editor flow.

Implementation file:

* `src/partitioner.cpp`

### Milestone 5 - Online installer integration - Partially completed

* [x] Add low-level helpers needed to calculate free ranges and create app/data partitions in RAM.
* [x] Add raw app/data flashing API needed by the installer.
* [x] Add manual `otadata` writer compatible with ESP-IDF OTA selection rules.
* [ ] Wire `onlineLauncher.cpp` to generate or modify the partition table in RAM.
* [ ] Wire online firmware flashing to use raw offsets from the generated table.
* [ ] Wire FAT/SPIFFS creation from firmware partition declarations.
* [ ] Write the generated table as part of the online install transaction.
* [ ] Call manual `otadata` activation after firmware and table verification.
* [ ] Reboot into the selected app from the new dynamic flow.

### Milestone 6 - Multi-app launcher - Partially completed

* [x] Add low-level helper to set boot target manually through `otadata`.
* [ ] Add app metadata storage.
* [ ] Create/update APP icons after install.
* [ ] Allow selecting a specific installed app.
* [ ] Wire APP selection to the manual `otadata` helper.

### Milestone 7 - Recovery and hardware validation - Not started

* [ ] Test interrupted download.
* [ ] Test interrupted flash.
* [ ] Test invalid image.
* [ ] Test full flash/no room.
* [ ] Test multiple apps.
* [ ] Test FAT/SPIFFS creation.
* [ ] Test fallback to TEST/FACTORY Launcher.
* [ ] Test on 4 MB, 8 MB, 16 MB, ESP32, ESP32-S3, and ESP32-P4 targets where supported.

## Current Verification

* [x] `pio run` passes for `m5stack-cardputer`.
* [ ] No hardware validation has been performed yet.
