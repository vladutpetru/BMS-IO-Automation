#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "app_state.h"
#include "wifi_ap.h"
#include "create_http_server.h"
#include "my_application.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    /* NVS: settings storage (and Wi-Fi driver calibration data) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition needs erasing (%s)", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Boil max + triggers must be loaded before the control task or HTTP server use them */
    ESP_ERROR_CHECK(app_state_init());

    /* Outputs (all OFF), ADC and UART */
    my_app_init();

    /* Start the radio BEFORE any relay can switch ON: the Wi-Fi PHY start-up current
     * peak plus relay coils switching at the same moment can trip the brownout detector. */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ret = wifi_ap_start();
    if (ret == ESP_OK)
    {
        http_server();
    }
    else
    {
        ESP_LOGE(TAG, "Wi-Fi AP not started (%s) - web UI unavailable, control keeps running",
                 esp_err_to_name(ret));
    }

    /* Temperature control (relays) starts last and runs even if Wi-Fi failed */
    if (xTaskCreate(my_application, "My Application", 4096, NULL, 10, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create application task - outputs stay OFF");
    }
}
