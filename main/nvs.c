#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "utils.h"
#include <stdint.h>
#include <string.h>

static const char *NVS_TAG = "NVS";

esp_err_t NVS_Write(const char* key, nvs_value_type_t type, const void* value, size_t len)
{
    nvs_handle_t nvsHandle;
    esp_err_t retVal;

    if (key == NULL || value == NULL)
    {
        ESP_LOGE(NVS_TAG, "NVS_Write called with NULL key or value");
        return ESP_ERR_INVALID_ARG;
    }

    retVal = nvs_open("STORAGE", NVS_READWRITE, &nvsHandle);
    if (retVal != ESP_OK)
    {
        ESP_LOGE(NVS_TAG, "Error (%s) opening NVS handle for Write (key '%s')", esp_err_to_name(retVal), key);
        return retVal;
    }

    switch (type)
    {
        case NVS_TYPE_STRING:
            retVal = nvs_set_str(nvsHandle, key, (const char*)value);
            break;
        case NVS_TYPE_U8:
            retVal = nvs_set_u8(nvsHandle, key, *(const uint8_t*)value);
            break;
        case NVS_TYPE_I8:
            retVal = nvs_set_i8(nvsHandle, key, *(const int8_t*)value);
            break;
        case NVS_TYPE_U16:
            retVal = nvs_set_u16(nvsHandle, key, *(const uint16_t*)value);
            break;
        case NVS_TYPE_I16:
            retVal = nvs_set_i16(nvsHandle, key, *(const int16_t*)value);
            break;
        case NVS_TYPE_U32:
            retVal = nvs_set_u32(nvsHandle, key, *(const uint32_t*)value);
            break;
        case NVS_TYPE_I32:
            retVal = nvs_set_i32(nvsHandle, key, *(const int32_t*)value);
            break;
        case NVS_TYPE_U64:
            retVal = nvs_set_u64(nvsHandle, key, *(const uint64_t*)value);
            break;
        case NVS_TYPE_I64:
            retVal = nvs_set_i64(nvsHandle, key, *(const int64_t*)value);
            break;
        case NVS_TYPE_BLOB:
            retVal = nvs_set_blob(nvsHandle, key, value, len);
            break;
        default:
            ESP_LOGE(NVS_TAG, "NVS_Write: unknown type %d for key '%s'", (int)type, key);
            retVal = ESP_ERR_INVALID_ARG;
            break;
    }

    if (retVal != ESP_OK)
    {
        ESP_LOGE(NVS_TAG, "Error (%s) writing key '%s' (type %d)", esp_err_to_name(retVal), key, (int)type);
        nvs_close(nvsHandle);
        return retVal;
    }

    retVal = nvs_commit(nvsHandle);
    if (retVal != ESP_OK)
    {
        ESP_LOGE(NVS_TAG, "Error (%s) committing key '%s'", esp_err_to_name(retVal), key);
    }
    else
    {
        ESP_LOGI(NVS_TAG, "Write commit OK for key '%s'", key);
    }

    nvs_close(nvsHandle);
    return retVal;
}

esp_err_t NVS_Read(const char* key, nvs_value_type_t type, void* value, size_t* len)
{
    nvs_handle_t nvsHandle;
    esp_err_t retVal;

    if (key == NULL || value == NULL)
    {
        ESP_LOGE(NVS_TAG, "NVS_Read called with NULL key or value");
        return ESP_ERR_INVALID_ARG;
    }
    if ((type == NVS_TYPE_STRING || type == NVS_TYPE_BLOB) && len == NULL)
    {
        ESP_LOGE(NVS_TAG, "NVS_Read: string/blob type requires a non-NULL len pointer (key '%s')", key);
        return ESP_ERR_INVALID_ARG;
    }

    retVal = nvs_open("STORAGE", NVS_READWRITE, &nvsHandle);
    if (retVal != ESP_OK)
    {
        ESP_LOGE(NVS_TAG, "Error (%s) opening NVS handle for Read (key '%s')", esp_err_to_name(retVal), key);
        return retVal;
    }

    switch (type)
    {
        case NVS_TYPE_STRING:
            retVal = nvs_get_str(nvsHandle, key, (char*)value, len);
            break;
        case NVS_TYPE_U8:
            retVal = nvs_get_u8(nvsHandle, key, (uint8_t*)value);
            break;
        case NVS_TYPE_I8:
            retVal = nvs_get_i8(nvsHandle, key, (int8_t*)value);
            break;
        case NVS_TYPE_U16:
            retVal = nvs_get_u16(nvsHandle, key, (uint16_t*)value);
            break;
        case NVS_TYPE_I16:
            retVal = nvs_get_i16(nvsHandle, key, (int16_t*)value);
            break;
        case NVS_TYPE_U32:
            retVal = nvs_get_u32(nvsHandle, key, (uint32_t*)value);
            break;
        case NVS_TYPE_I32:
            retVal = nvs_get_i32(nvsHandle, key, (int32_t*)value);
            break;
        case NVS_TYPE_U64:
            retVal = nvs_get_u64(nvsHandle, key, (uint64_t*)value);
            break;
        case NVS_TYPE_I64:
            retVal = nvs_get_i64(nvsHandle, key, (int64_t*)value);
            break;
        case NVS_TYPE_BLOB:
            retVal = nvs_get_blob(nvsHandle, key, value, len);
            break;
        default:
            ESP_LOGE(NVS_TAG, "NVS_Read: unknown type %d for key '%s'", (int)type, key);
            retVal = ESP_ERR_INVALID_ARG;
            break;
    }

    if (retVal == ESP_OK)
    {
        ESP_LOGI(NVS_TAG, "Read OK for key '%s'", key);
    }
    else if (retVal == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(NVS_TAG, "Key '%s' not found in NVS", key);
    }
    else
    {
        ESP_LOGE(NVS_TAG, "Error (%s) reading key '%s' (type %d)", esp_err_to_name(retVal), key, (int)type);
    }

    nvs_close(nvsHandle);
    return retVal;
}

/* --- Typed convenience wrappers, kept for existing call sites --- */

void NVS_Write_String(const char* key, const char* stringVal)
{
    /* Original signature is void; callers that need the result should switch to NVS_Write() directly. */
    (void)NVS_Write(key, NVS_TYPE_STRING, stringVal, 0);
}

esp_err_t NVS_Read_String(const char* key, char* value, char max_len)
{
    size_t len = (size_t)max_len;
    return NVS_Read(key, NVS_TYPE_STRING, value, &len);
}

esp_err_t NVS_Write_U32(const char* key, uint32_t value)
{
    return NVS_Write(key, NVS_TYPE_U32, &value, sizeof(value));
}

esp_err_t NVS_Read_U32(const char* key, uint32_t* value)
{
    return NVS_Read(key, NVS_TYPE_U32, value, NULL);
}