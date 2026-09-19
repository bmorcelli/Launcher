// Native shim for esp-idf's nvs.h. settings.cpp (compiled for real in the
// native harness, see native/src/stubs.cpp) reads/writes NVS through these
// calls; here they all just fail cleanly so callers fall back to defaults,
// same tier as the rest of native/sources/ (just enough to link and behave
// like "nothing is persisted").
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

typedef uint32_t nvs_handle_t;
typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_HANDLE 0x1103

typedef enum {
    NVS_READONLY,
    NVS_READWRITE,
} nvs_open_mode_t;

typedef enum {
    NVS_TYPE_U8 = 0x01,
    NVS_TYPE_I8 = 0x11,
    NVS_TYPE_U16 = 0x02,
    NVS_TYPE_I16 = 0x12,
    NVS_TYPE_U32 = 0x04,
    NVS_TYPE_I32 = 0x14,
    NVS_TYPE_U64 = 0x08,
    NVS_TYPE_I64 = 0x18,
    NVS_TYPE_STR = 0x21,
    NVS_TYPE_BLOB = 0x42,
    NVS_TYPE_ANY = 0xff,
} nvs_type_t;

struct nvs_entry_info_t {
    char namespace_name[16];
    char key[16];
    nvs_type_t type;
};

typedef struct nvs_opaque_iterator_t *nvs_iterator_t;

inline esp_err_t nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *out) {
    if (out) *out = 1;
    return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
inline esp_err_t nvs_erase_all(nvs_handle_t) { return ESP_OK; }
inline esp_err_t nvs_erase_key(nvs_handle_t, const char *) { return ESP_OK; }

inline esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline esp_err_t nvs_set_u8(nvs_handle_t, const char *, uint8_t) { return ESP_OK; }
inline esp_err_t nvs_get_i8(nvs_handle_t, const char *, int8_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline esp_err_t nvs_set_i8(nvs_handle_t, const char *, int8_t) { return ESP_OK; }
inline esp_err_t nvs_get_u16(nvs_handle_t, const char *, uint16_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline esp_err_t nvs_set_u16(nvs_handle_t, const char *, uint16_t) { return ESP_OK; }
inline esp_err_t nvs_get_i16(nvs_handle_t, const char *, int16_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline esp_err_t nvs_set_i16(nvs_handle_t, const char *, int16_t) { return ESP_OK; }
inline esp_err_t nvs_get_u32(nvs_handle_t, const char *, uint32_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline esp_err_t nvs_set_u32(nvs_handle_t, const char *, uint32_t) { return ESP_OK; }
inline esp_err_t nvs_get_i32(nvs_handle_t, const char *, int32_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline esp_err_t nvs_set_i32(nvs_handle_t, const char *, int32_t) { return ESP_OK; }
inline esp_err_t nvs_get_u64(nvs_handle_t, const char *, uint64_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline esp_err_t nvs_set_u64(nvs_handle_t, const char *, uint64_t) { return ESP_OK; }
inline esp_err_t nvs_get_i64(nvs_handle_t, const char *, int64_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline esp_err_t nvs_set_i64(nvs_handle_t, const char *, int64_t) { return ESP_OK; }

inline esp_err_t nvs_get_str(nvs_handle_t, const char *, char *out, size_t *len) {
    if (out && len && *len) out[0] = '\0';
    if (len) *len = 1;
    return ESP_ERR_NVS_NOT_FOUND;
}
inline esp_err_t nvs_set_str(nvs_handle_t, const char *, const char *) { return ESP_OK; }
inline esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *len) {
    if (len) *len = 0;
    return ESP_ERR_NVS_NOT_FOUND;
}
inline esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t) { return ESP_OK; }

inline nvs_iterator_t nvs_entry_find(const char *, const char *, nvs_type_t) { return nullptr; }
inline esp_err_t nvs_entry_next(nvs_iterator_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline esp_err_t nvs_entry_info(nvs_iterator_t, nvs_entry_info_t *) { return ESP_ERR_NVS_NOT_FOUND; }
inline void nvs_release_iterator(nvs_iterator_t) {}

inline const char *esp_err_to_name(esp_err_t) { return "ESP_FAIL (native stub)"; }
