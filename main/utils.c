#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

void NVS_Write_String(const char* key, const char* stringVal)
{
    nvs_handle_t nvsHandle;
    esp_err_t retVal;
 
    retVal = nvs_open("STORAGE", NVS_READWRITE, &nvsHandle);
    if(retVal != ESP_OK)
    {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle for Write", esp_err_to_name(retVal));
        return;
    }

    ESP_LOGI("NVS", "opening NVS Write handle Done");
    retVal = nvs_set_str(nvsHandle, key, stringVal);
    if(retVal != ESP_OK)
    {
        ESP_LOGE("NVS", "Error (%s) Can not write/set value: %s", esp_err_to_name(retVal), stringVal);
    }

    retVal = nvs_commit(nvsHandle);
    if(retVal != ESP_OK)
    {
        ESP_LOGE("NVS", "Error (%s) Can not commit - write", esp_err_to_name(retVal));
    }
    else
    {
        ESP_LOGI("NVS", "Write Commit Done!");
    }

    nvs_close(nvsHandle);
}

esp_err_t NVS_Read_String(const char* key, char* value, char max_len)
{
    nvs_handle_t nvsHandle;
    esp_err_t retVal;
    size_t len = (size_t)max_len;

    retVal = nvs_open("STORAGE", NVS_READWRITE, &nvsHandle);
    if(retVal != ESP_OK)
    {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle for Read", esp_err_to_name(retVal));
        return retVal;
    }

    ESP_LOGI("NVS", "opening NVS Read handle Done");
    retVal = nvs_get_str(nvsHandle, key, value, &len);
    if(retVal == ESP_OK)
    {
        ESP_LOGI("NVS", "Read OK for key '%s'", key);
    }
    else
    {
        ESP_LOGE("NVS", "Error (%s) Can not read/get value for key '%s'", esp_err_to_name(retVal), key);
    }

    nvs_close(nvsHandle);
    return retVal;
}