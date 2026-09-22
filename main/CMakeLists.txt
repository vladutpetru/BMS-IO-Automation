idf_component_register(SRCS "main_app.c"
                            "wifi_scan.c"
                            "create_http_server.c"
                            "sta_ap_wifi_network.c"
                            "global_variables.c"
                            "app_nvs.c"
                            "app_state.c"
                            "my_application.c"
                            "sensors.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi
                             esp_netif
                             nvs_flash
                             esp_event
                             esp_http_server
                             spiffs
                             esp_timer
                             driver
                             esp_adc
                             json)
