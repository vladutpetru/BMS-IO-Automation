idf_component_register(SRCS "main_app.c"
                        "wifi_scan.c"
                        "create_http_server.c"
                        "sta_ap_wifi_network.c"
                        "global_variables.c"
                        "app_nvs.c"
                        "my_application.c"
                        "sensors.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi
                    REQUIRES nvs_flash
                    REQUIRES esp_event
                    REQUIRES esp_http_server
                    REQUIRES spiffs
                    REQUIRES esp_timer
                    REQUIRES driver
                    REQUIRES esp_adc)
