#include "esp_event.h"
#include "esp_wifi.h"
#include "global.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>
#include "esp_netif_ip_addr.h"
#include "esp_netif.h"
#include "lwip/ip_addr.h"
#include "app_nvs.h"

#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY
esp_netif_t *ap_netif = NULL;
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "WIFI";
static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void initialise_wifi(void)
{
	esp_log_level_set("wifi", ESP_LOG_WARN);
	wifi_event_group = xEventGroupCreate();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
	assert(ap_netif);
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    esp_netif_ip_info_t ipInfo;
    IP4_ADDR(&ipInfo.ip, 192, 168, 4, 5);
    IP4_ADDR(&ipInfo.gw, 192, 168, 4, 1);
    IP4_ADDR(&ipInfo.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ipInfo));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));  
	esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
	assert(sta_netif);
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
	ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL) );
	ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    ESP_LOGI(TAG, "My IP: " IPSTR "\n", IP2STR(&ipInfo.ip));
}

void wifi_ap_sta_init(void)
{
    char buffer[20] = {0};
    wifi_config_t ap_config = { 0 };
	strcpy((char *)ap_config.ap.ssid,WIFI_SSID_SERVER);
	strcpy((char *)ap_config.ap.password, WIFI_PASSWORD_SERVER);
	ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
	ap_config.ap.ssid_len = strlen(WIFI_SSID_SERVER);
	ap_config.ap.max_connection = 10;
	ap_config.ap.channel = 2;

	if (strlen(WIFI_PASSWORD_SERVER) == 0) {
		ap_config.ap.authmode = WIFI_AUTH_OPEN;
	}
    memset(&buffer[0], 0, 20);
    NVS_Read_String("STA_SSID", &buffer[0], 20);
	wifi_config_t sta_config = { 0 };
	strcpy((char *)sta_config.sta.ssid, &buffer[0]);
    memset(&buffer[0], 0, 20);
    NVS_Read_String("STA_PWD", &buffer[0], 20);
	strcpy((char *)sta_config.sta.password, &buffer[0]);

	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config) );
	esp_wifi_start();
    wifi_scan();
	esp_wifi_connect();
	int bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
								   pdFALSE, pdTRUE, 4000 / portTICK_PERIOD_MS);
	ESP_LOGI(TAG, "bits=%x", bits);
	if (bits) {
		ESP_LOGI(TAG, "WIFI_MODE_STA connected. SSID:%s password:%s",
			 (char *)sta_config.sta.ssid, (char *)sta_config.sta.password);
	} else {
		ESP_LOGI(TAG, "WIFI_MODE_STA can't connected. SSID:%s password:%s",
			 (char *)sta_config.sta.ssid, (char *)sta_config.sta.password);
        ESP_LOGI(TAG, "Connect as AP");
        esp_wifi_stop();
        ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
        ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config) );
        esp_wifi_start();
        ESP_LOGI(TAG, "WIFI_MODE_AP started. SSID:%s password:%s channel:%d",
	        WIFI_SSID_SERVER, WIFI_PASSWORD_SERVER, 2);
	}
}