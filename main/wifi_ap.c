#include <string.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "wifi_ap.h"

static const char *TAG = "WIFI_AP";

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    if (id == WIFI_EVENT_AP_STACONNECTED)
    {
        const wifi_event_ap_staconnected_t *e = (const wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "Station " MACSTR " connected (AID %d)", MAC2STR(e->mac), e->aid);
    }
    else if (id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        const wifi_event_ap_stadisconnected_t *e = (const wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "Station " MACSTR " disconnected (AID %d)", MAC2STR(e->mac), e->aid);
    }
}

esp_err_t wifi_ap_start(void)
{
    const char *ssid = CONFIG_APP_WIFI_AP_SSID;
    const char *pwd  = CONFIG_APP_WIFI_AP_PASSWORD;
    size_t ssid_len = strlen(ssid);
    size_t pwd_len  = strlen(pwd);

    if (ssid_len == 0 || ssid_len > 32)
    {
        ESP_LOGE(TAG, "AP SSID must be 1-32 characters (menuconfig -> BMS IO Automation)");
        return ESP_ERR_INVALID_ARG;
    }
    if (pwd_len != 0 && (pwd_len < 8 || pwd_len > 63))
    {
        ESP_LOGE(TAG, "AP password must be empty or 8-63 characters (menuconfig -> BMS IO Automation)");
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *netif = esp_netif_create_default_wifi_ap();
    if (netif == NULL)
    {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap failed");
        return ESP_FAIL;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_init failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Registering Wi-Fi event handler failed (%s)", esp_err_to_name(err));
        return err;
    }

    /* Config comes from menuconfig on every boot; don't keep a copy in NVS */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_storage failed (%s)", esp_err_to_name(err));
        return err;
    }

    wifi_config_t cfg = { 0 };
    memcpy(cfg.ap.ssid, ssid, ssid_len);
    cfg.ap.ssid_len = (uint8_t)ssid_len;
    memcpy(cfg.ap.password, pwd, pwd_len);
    cfg.ap.channel = CONFIG_APP_WIFI_AP_CHANNEL;
    cfg.ap.max_connection = CONFIG_APP_WIFI_AP_MAX_CONN;
    cfg.ap.authmode = (pwd_len == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    cfg.ap.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed (%s)", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_config failed (%s)", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_start failed (%s)", esp_err_to_name(err));
        return err;
    }

    esp_netif_ip_info_t ip;
    err = esp_netif_get_ip_info(netif, &ip);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "AP '%s' started, but reading its IP failed (%s)", ssid, esp_err_to_name(err));
        return ESP_OK;
    }
    ESP_LOGI(TAG, "AP '%s' started on channel %d (%s)", ssid, CONFIG_APP_WIFI_AP_CHANNEL,
             pwd_len ? "WPA2" : "open");
    ESP_LOGI(TAG, "Connect to '%s' and open http://" IPSTR "/", ssid, IP2STR(&ip.ip));
    return ESP_OK;
}
