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
    }
    else
    {
        printf("opening NVS Write handle Done \r\n");
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
       
    }
 
    nvs_close(nvsHandle);
   
}

esp_err_t NVS_Read_String(const char* key, char* value, char max_len)
{
    nvs_handle_t nvsHandle;
    esp_err_t retVal;
    size_t len;
    char* savedData = NULL;
    len = sizeof(savedData) + 1;
 
    retVal = nvs_open("STORAGE", NVS_READWRITE, &nvsHandle);
    if(retVal != ESP_OK)
    {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle for Write", esp_err_to_name(retVal));
    }
    else
    {
        printf("opening NVS Read handle Done \r\n");
        retVal = nvs_get_str(nvsHandle, key, savedData, &len);
        if (len > max_len) len = max_len;
        retVal = nvs_get_str(nvsHandle, key, value, &len);
        if(retVal == ESP_OK)
        {
            ESP_LOGI("NVS", "Msg (%s) Can read/get value ", esp_err_to_name(retVal));
        }
        else
            ESP_LOGE("NVS", "Error (%s) Can not read/get value", esp_err_to_name(retVal));
    }
 
    nvs_close(nvsHandle);
    return retVal;
}