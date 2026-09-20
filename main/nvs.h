#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/* Value kind stored under a given NVS key - picks which nvs_set_*/nvs_get_* the generic functions call */
typedef enum {
    NVS_TYPE_STRING = 0,
    NVS_TYPE_U8,
    NVS_TYPE_I8,
    NVS_TYPE_U16,
    NVS_TYPE_I16,
    NVS_TYPE_U32,
    NVS_TYPE_I32,
    NVS_TYPE_U64,
    NVS_TYPE_I64,
    NVS_TYPE_BLOB
} nvs_value_type_t;

/*
 * Generic NVS write.
 * - NVS_TYPE_STRING: value = const char* (null-terminated), len ignored.
 * - NVS_TYPE_BLOB:    value = const void*, len = number of bytes to store.
 * - fixed-width numeric types: value = pointer to a variable of the matching width
 *   (e.g. NVS_TYPE_U32 -> const uint32_t*), len ignored.
 */
esp_err_t NVS_Write(const char* key, nvs_value_type_t type, const void* value, size_t len);

/*
 * Generic NVS read.
 * - NVS_TYPE_STRING: value = char* buffer, *len = buffer size in, actual string length out.
 * - NVS_TYPE_BLOB:    value = void* buffer, *len = buffer size in, actual blob size out.
 * - fixed-width numeric types: value = pointer to a variable of the matching width, len ignored (may be NULL).
 * Returns ESP_ERR_NVS_NOT_FOUND if the key has never been written.
 */
esp_err_t NVS_Read(const char* key, nvs_value_type_t type, void* value, size_t* len);

/* --- Typed convenience wrappers, kept for existing call sites --- */
void NVS_Write_String(const char* key, const char* stringVal);
esp_err_t NVS_Read_String(const char* key, char* value, char max_len);

esp_err_t NVS_Write_U32(const char* key, uint32_t value);
esp_err_t NVS_Read_U32(const char* key, uint32_t* value);