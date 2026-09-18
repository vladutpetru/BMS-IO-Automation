#include "esp_wifi.h"

#define WIFI_SSID_SERVER "IDF"
#define WIFI_PASSWORD_SERVER "12345678"
#define CONFIG_ESP_MAXIMUM_RETRY 5
#define CONFIG_EXAMPLE_SCAN_LIST_SIZE 10
#define DEFAULT_SCAN_LIST_SIZE CONFIG_EXAMPLE_SCAN_LIST_SIZE

extern wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
extern uint16_t ap_count;
void wifi_scan(void);
void http_server(void);
void wifi_ap_sta_init(void);
void initialise_wifi(void);