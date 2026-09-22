#ifndef WIFI_AP_H
#define WIFI_AP_H

#include "esp_err.h"

/* Starts the ESP32 as a Wi-Fi access point (SSID/password from menuconfig
 * -> "BMS IO Automation") and logs the IP address to open in the browser.
 * Requires esp_netif_init() and esp_event_loop_create_default() first. */
esp_err_t wifi_ap_start(void);

#endif /* WIFI_AP_H */
