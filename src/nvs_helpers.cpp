#include "nvs_helpers.h"
#include "idf/launcher_platform.h"
#include <cstring>
#include <memory>

namespace lnvs {

Handle::Handle(const char *ns, bool write) { open(ns, write); }

Handle::~Handle() {
    if (handle) nvs_close(handle);
}

bool Handle::open(const char *ns, bool write) {
    if (handle) nvs_close(handle);
    esp_err_t err = nvs_open(ns, write ? NVS_READWRITE : NVS_READONLY, &handle);
    if (err != ESP_OK) {
        handle = 0;
        // A missing namespace on a read is routine (nothing has written it yet), so
        // it is not worth a line on the console; anything else is.
        if (err != ESP_ERR_NVS_NOT_FOUND) { launcherConsolePrintf("NVS: open %s failed (%d)\n", ns, err); }
    }
    return handle != 0;
}

bool Handle::commit() { return handle && nvs_commit(handle) == ESP_OK; }

std::vector<String> keys(const char *ns, nvs_type_t filter) {
    std::vector<String> found;
    nvs_iterator_t it = nullptr;
    esp_err_t err = nvs_entry_find("nvs", ns, NVS_TYPE_ANY, &it);
    while (err == ESP_OK && it != nullptr) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (filter == NVS_TYPE_ANY || info.type == filter) found.push_back(String(info.key));
        err = nvs_entry_next(&it);
    }
    if (it) nvs_release_iterator(it);
    return found;
}

bool exists(const char *ns) {
    nvs_iterator_t it = nullptr;
    bool found = nvs_entry_find("nvs", ns, NVS_TYPE_ANY, &it) == ESP_OK;
    if (it) nvs_release_iterator(it);
    return found;
}

bool eraseNamespace(const char *ns) {
    if (!exists(ns)) return true;

    Handle handle(ns, true);
    if (!handle) return false;
    if (nvs_erase_all(handle.raw()) != ESP_OK) return false;
    return handle.commit();
}

String getString(nvs_handle_t handle, const char *key, size_t maxLen) {
    size_t len = 0;
    if (nvs_get_str(handle, key, nullptr, &len) != ESP_OK) return "";
    if (len == 0 || (maxLen && len > maxLen + 1)) return "";

    std::unique_ptr<char[]> buffer(new (std::nothrow) char[len]);
    if (!buffer) return "";
    if (nvs_get_str(handle, key, buffer.get(), &len) != ESP_OK) return "";
    return String(buffer.get());
}

String getString(const char *ns, const char *key, size_t maxLen) {
    Handle handle(ns, false);
    if (!handle) return "";
    return getString(handle.raw(), key, maxLen);
}

bool setString(nvs_handle_t handle, const char *key, const char *value) {
    return nvs_set_str(handle, key, value) == ESP_OK;
}

bool setString(const char *ns, const char *key, const char *value) {
    Handle handle(ns, true);
    if (!handle) return false;
    if (!setString(handle.raw(), key, value)) return false;
    return handle.commit();
}

bool eraseKey(nvs_handle_t handle, const char *key) {
    esp_err_t err = nvs_erase_key(handle, key);
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

bool getBool(nvs_handle_t handle, const char *key, bool &value) {
    uint8_t stored = 0;
    if (nvs_get_u8(handle, key, &stored) != ESP_OK) return false;
    value = stored != 0;
    return true;
}

bool setBool(nvs_handle_t handle, const char *key, bool value) {
    return nvs_set_u8(handle, key, value ? 1 : 0) == ESP_OK;
}

bool getInt(nvs_handle_t handle, const char *key, int &value) {
    int32_t stored = 0;
    if (nvs_get_i32(handle, key, &stored) != ESP_OK) return false;
    value = stored;
    return true;
}

bool setInt(nvs_handle_t handle, const char *key, int value) {
    return nvs_set_i32(handle, key, static_cast<int32_t>(value)) == ESP_OK;
}

namespace {
struct TypeName {
    nvs_type_t type;
    const char *name;
};

constexpr TypeName kTypeNames[] = {
    {NVS_TYPE_U8,   "u8"  },
    {NVS_TYPE_I8,   "i8"  },
    {NVS_TYPE_U16,  "u16" },
    {NVS_TYPE_I16,  "i16" },
    {NVS_TYPE_U32,  "u32" },
    {NVS_TYPE_I32,  "i32" },
    {NVS_TYPE_U64,  "u64" },
    {NVS_TYPE_I64,  "i64" },
    {NVS_TYPE_STR,  "str" },
    {NVS_TYPE_BLOB, "blob"},
};
} // namespace

const char *typeName(nvs_type_t type) {
    for (const TypeName &entry : kTypeNames) {
        if (entry.type == type) return entry.name;
    }
    return "?";
}

nvs_type_t typeFromName(const char *name) {
    if (!name) return NVS_TYPE_ANY;
    for (const TypeName &entry : kTypeNames) {
        if (strcmp(entry.name, name) == 0) return entry.type;
    }
    return NVS_TYPE_ANY;
}

bool getScalar(nvs_handle_t handle, const char *key, nvs_type_t type, int64_t &value) {
    esp_err_t err = ESP_ERR_NVS_TYPE_MISMATCH;
    switch (type) {
        case NVS_TYPE_U8: {
            uint8_t v = 0;
            err = nvs_get_u8(handle, key, &v);
            value = v;
            break;
        }
        case NVS_TYPE_I8: {
            int8_t v = 0;
            err = nvs_get_i8(handle, key, &v);
            value = v;
            break;
        }
        case NVS_TYPE_U16: {
            uint16_t v = 0;
            err = nvs_get_u16(handle, key, &v);
            value = v;
            break;
        }
        case NVS_TYPE_I16: {
            int16_t v = 0;
            err = nvs_get_i16(handle, key, &v);
            value = v;
            break;
        }
        case NVS_TYPE_U32: {
            uint32_t v = 0;
            err = nvs_get_u32(handle, key, &v);
            value = v;
            break;
        }
        case NVS_TYPE_I32: {
            int32_t v = 0;
            err = nvs_get_i32(handle, key, &v);
            value = v;
            break;
        }
        case NVS_TYPE_U64: {
            uint64_t v = 0;
            err = nvs_get_u64(handle, key, &v);
            value = static_cast<int64_t>(v);
            break;
        }
        case NVS_TYPE_I64: {
            int64_t v = 0;
            err = nvs_get_i64(handle, key, &v);
            value = v;
            break;
        }
        default: return false;
    }
    return err == ESP_OK;
}

bool setScalar(nvs_handle_t handle, const char *key, nvs_type_t type, int64_t value) {
    esp_err_t err = ESP_ERR_NVS_TYPE_MISMATCH;
    switch (type) {
        case NVS_TYPE_U8: err = nvs_set_u8(handle, key, static_cast<uint8_t>(value)); break;
        case NVS_TYPE_I8: err = nvs_set_i8(handle, key, static_cast<int8_t>(value)); break;
        case NVS_TYPE_U16: err = nvs_set_u16(handle, key, static_cast<uint16_t>(value)); break;
        case NVS_TYPE_I16: err = nvs_set_i16(handle, key, static_cast<int16_t>(value)); break;
        case NVS_TYPE_U32: err = nvs_set_u32(handle, key, static_cast<uint32_t>(value)); break;
        case NVS_TYPE_I32: err = nvs_set_i32(handle, key, static_cast<int32_t>(value)); break;
        case NVS_TYPE_U64: err = nvs_set_u64(handle, key, static_cast<uint64_t>(value)); break;
        case NVS_TYPE_I64: err = nvs_set_i64(handle, key, value); break;
        default: return false;
    }
    return err == ESP_OK;
}

bool copyBlob(nvs_handle_t from, const char *fromKey, nvs_handle_t to, const char *toKey, size_t maxLen) {
    size_t len = 0;
    esp_err_t err = nvs_get_blob(from, fromKey, nullptr, &len);
    if (err != ESP_OK) {
        launcherConsolePrintf("NVS: size of %s failed (%d)\n", fromKey, err);
        return false;
    }
    if (len == 0 || len > maxLen) {
        launcherConsolePrintf("NVS: %s is %u bytes, not copied\n", fromKey, (unsigned)len);
        return false;
    }

    std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[len]);
    if (!buffer) return false;

    err = nvs_get_blob(from, fromKey, buffer.get(), &len);
    if (err == ESP_OK) err = nvs_set_blob(to, toKey, buffer.get(), len);
    if (err != ESP_OK) {
        launcherConsolePrintf("NVS: copy %s failed (%d)\n", fromKey, err);
        return false;
    }
    return true;
}

} // namespace lnvs
