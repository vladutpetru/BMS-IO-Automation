#ifndef APP_NVS_H
#define APP_NVS_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "nvs.h"        /* ESP_ERR_NVS_NOT_FOUND and the other NVS error codes */

typedef enum {
    APP_NVS_STRING = 0,
    APP_NVS_U8,
    APP_NVS_I8,
    APP_NVS_U16,
    APP_NVS_I16,
    APP_NVS_U32,
    APP_NVS_I32,
    APP_NVS_U64,
    APP_NVS_I64,
    APP_NVS_BLOB
} nvs_value_type_t;

/*
 * Generic NVS write.
 * - APP_NVS_STRING: value = const char* (null-terminated), len ignored.
 * - APP_NVS_BLOB:    value = const void*, len = number of bytes to store.
 * - fixed-width numeric types: value = pointer to a variable of the matching width
 *   (e.g. APP_NVS_U32 -> const uint32_t*), len ignored.
 */
esp_err_t NVS_Write(const char* key, nvs_value_type_t type, const void* value, size_t len);

/*
 * Generic NVS read.
 * - APP_NVS_STRING: value = char* buffer, *len = buffer size in, actual string length out.
 * - APP_NVS_BLOB:    value = void* buffer, *len = buffer size in, actual blob size out.
 * - fixed-width numeric types: value = pointer to a variable of the matching width, len ignored (may be NULL).
 * Returns ESP_ERR_NVS_NOT_FOUND if the key (or the whole namespace) has never been written.
 */
esp_err_t NVS_Read(const char* key, nvs_value_type_t type, void* value, size_t* len);

#endif /* APP_NVS_H */
