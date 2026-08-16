#ifndef LAUNCHER_NVS_HELPERS_H
#define LAUNCHER_NVS_HELPERS_H

#include <Arduino.h>
#include <nvs.h>
#include <vector>

// Single entry point for NVS access in the Launcher.
//
// Two reasons it exists. The same four dances -- open a namespace, walk its keys,
// read a string of unknown length, wipe a namespace -- were reimplemented in
// settings.cpp, app_registry.cpp, webInterface.cpp and ble_bonds.cpp. And the
// project used to link both NVS APIs of the IDF at once: the C one (nvs_open,
// nvs_get_*) and the C++ one (nvs::open_nvs_handle, get_item<T>), about 2.6KB of
// glue for two ways of doing the same thing.
//
// Everything here is the C API. Nothing in the project should call
// nvs::open_nvs_handle or include nvs_handle.hpp anymore.
namespace lnvs {

// RAII around nvs_open. Evaluates to false when the namespace could not be opened,
// which for a read-only open normally just means it does not exist yet.
class Handle {
public:
    Handle() = default;
    Handle(const char *ns, bool write);
    ~Handle();
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

    // For the callers that only know halfway through whether they need it: opening
    // for writing creates the namespace, which is worth avoiding when the answer
    // turns out to be no.
    bool open(const char *ns, bool write);

    explicit operator bool() const { return handle != 0; }
    nvs_handle_t raw() const { return handle; }
    bool commit();

private:
    nvs_handle_t handle = 0;
};

// Every key in `ns`, optionally only those of one type. The list is collected up
// front because an NVS iterator must not be alive while the namespace is written,
// so callers can erase or overwrite while walking the result.
//
// Filtering happens here rather than through nvs_entry_find: NVS_TYPE_BLOB matches
// the BLOB_DATA item type, not the index entry a small blob is actually listed
// under, so asking the iterator for blobs finds nothing.
std::vector<String> keys(const char *ns, nvs_type_t filter = NVS_TYPE_ANY);

// True when the namespace holds at least one entry.
bool exists(const char *ns);

// Wipes every key of a namespace. A namespace that does not exist is left alone
// instead of being created just to be emptied.
bool eraseNamespace(const char *ns);

// Reads a string of any length; empty when the key is missing. A non-zero maxLen
// rejects (rather than truncates) anything longer, matching what a fixed-size
// buffer read used to do.
String getString(nvs_handle_t handle, const char *key, size_t maxLen = 0);
String getString(const char *ns, const char *key, size_t maxLen = 0);

// Always writes, including empty strings -- use eraseKey to remove a key. The
// namespace overload opens, writes and commits on its own.
bool setString(nvs_handle_t handle, const char *key, const char *value);
bool setString(const char *ns, const char *key, const char *value);

// Missing keys count as erased.
bool eraseKey(nvs_handle_t handle, const char *key);

// bool and int are stored the way the old nvs::NVSHandle::set_item<T> stored them --
// U8 for bool, I32 for int -- so keys written by earlier firmware still read back.
// Other widths (uint16_t, int8_t, ...) map 1:1 onto nvs_get_*/nvs_set_* already.
bool getBool(nvs_handle_t handle, const char *key, bool &value);
bool setBool(nvs_handle_t handle, const char *key, bool value);
bool getInt(nvs_handle_t handle, const char *key, int &value);
bool setInt(nvs_handle_t handle, const char *key, int value);

// Moves a blob between handles without interpreting it. Blobs larger than maxLen
// are refused rather than copied.
bool copyBlob(nvs_handle_t from, const char *fromKey, nvs_handle_t to, const char *toKey, size_t maxLen);

// Type-agnostic scalar access, for code that discovers the type at runtime instead
// of knowing it -- the web NVS editor. Keeps one switch over the eight integer
// widths in one place rather than one per read and one per write. NVS_TYPE_ANY is
// returned for names that are not scalars ("str", "blob") or are unknown.
const char *typeName(nvs_type_t type);
nvs_type_t typeFromName(const char *name);
bool getScalar(nvs_handle_t handle, const char *key, nvs_type_t type, int64_t &value);
bool setScalar(nvs_handle_t handle, const char *key, nvs_type_t type, int64_t value);

} // namespace lnvs

#endif /* LAUNCHER_NVS_HELPERS_H */
