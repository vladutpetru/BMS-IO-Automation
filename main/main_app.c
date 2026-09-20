/* Scan Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
    This example shows how to scan for available set of APs.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "global.h"
#include "app_nvs.h"
#include "my_application.h"

wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
uint16_t ap_count = 0;

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    ESP_ERROR_CHECK(esp_netif_init());
    esp_event_loop_create_default();
    /* Initialisation of NVS values only if not exist */
    char buffer[20] = {0};
    if (ESP_ERR_NVS_NOT_FOUND == NVS_Read_String("STA_SSID", &buffer[0], 20) )
        NVS_Write_String("STA_SSID", "3r3r3r3r");
    if (ESP_ERR_NVS_NOT_FOUND == NVS_Read_String("STA_PWD", &buffer[0], 20) )
        NVS_Write_String("STA_PWD", "3r3r3r3r");
    /* --------------------------- */
    vTaskDelay(500 / portTICK_PERIOD_MS);
    initialise_wifi();
    vTaskDelay(500 / portTICK_PERIOD_MS);
    wifi_ap_sta_init();
    vTaskDelay(500 / portTICK_PERIOD_MS);
    http_server();
    my_app_init();
    xTaskCreate(my_application, "My Application", 4096, NULL, 10, NULL);
}