#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "global.h"
#include "app_nvs.h"
#include "app_state.h"
#include "my_application.h"

static const char *TAG = "MAIN";

wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
uint16_t ap_count = 0;

void app_main(void)
{
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Default STA credentials only if not stored yet */
    char buffer[20] = { 0 };
    if (NVS_Read_String("STA_SSID", buffer, sizeof(buffer)) == ESP_ERR_NVS_NOT_FOUND)
    {
        NVS_Write_String("STA_SSID", "3r3r3r3r");
    }
    if (NVS_Read_String("STA_PWD", buffer, sizeof(buffer)) == ESP_ERR_NVS_NOT_FOUND)
    {
        NVS_Write_String("STA_PWD", "3r3r3r3r");
    }

    /* Boil max + triggers must be loaded before the HTTP server serves them */
    ESP_ERROR_CHECK(app_state_init());

    vTaskDelay(pdMS_TO_TICKS(500));
    initialise_wifi();
    vTaskDelay(pdMS_TO_TICKS(500));
    wifi_ap_sta_init();
    vTaskDelay(pdMS_TO_TICKS(500));
    http_server();
    my_app_init();

    if (xTaskCreate(my_application, "My Application", 4096, NULL, 10, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create application task");
    }
}
