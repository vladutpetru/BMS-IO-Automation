#include "global.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sys/param.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "global_variables.h"
#include <stdio.h>
#include "utils.h"

static const char *TAG = "HTTP_Server";

char buffer[4096];

esp_err_t send_web_page(httpd_req_t *req, FILE * file)
{
    int response;
    int bytes;
    memset(&buffer, 0, 4096);
    fseek(file, 0L, SEEK_END);
    int file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    bytes = fread(buffer, 1,file_size , file);
    if (file_size > 4095)
    {
        ESP_LOGW(TAG,"File size: %d, buffer size 4095", file_size);
    }
    response = httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN);
    (void)(bytes);
    return response;
}

esp_err_t send_html_page(httpd_req_t *req, char page)
{
    int res = 0;
    FILE *file = NULL;
    esp_vfs_spiffs_conf_t config = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_vfs_spiffs_register(&config);

    switch (page)
    {
    case 1:
        file = fopen("/spiffs/read.html", "r");
        break;
    
    case 2:
        file = fopen("/spiffs/set.html", "r");
        break;

    case 3:
        file = fopen("/spiffs/wifi_manager.html", "r");
        break;
    
    case 4:
        file = fopen("/spiffs/httpGetAsync.js", "r");
        break;

    case 5:
        file = fopen("/spiffs/update_table_read.js", "r");
        break;
    
    case 6:
        file = fopen("/spiffs/update_table_set.js", "r");
        break;
    default:
        break;
    }
  
    if(file ==NULL)
    {
        ESP_LOGE(TAG,"File does not exist!");
        res = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
    }
    else 
    {
        res=send_web_page(req,file);
        fclose(file);
    }
    esp_vfs_spiffs_unregister(NULL);
    return res;
}

static esp_err_t _set(httpd_req_t *req)
{
    char *buf;
    size_t buf_len;
    int res = ESP_FAIL;
    char param[32];
    buf_len = strlen(req->uri) + 1;

    if (buf_len > 1)
    {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            ESP_LOGI(TAG, "SET - Found URL query: %s", buf);
            if (httpd_query_key_value(buf, "ssid", param, sizeof(param)) == ESP_OK) 
            {
                if (strlen(param)) 
                {
                    NVS_Write_String("STA_SSID", param);
                    res = httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
                }
            }
            else if (httpd_query_key_value(buf, "pwd", param, sizeof(param)) == ESP_OK) 
            {
                if (strlen(param))
                {
                    NVS_Write_String("STA_PWD", param);
                    res = httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
                }
            }
            else if (httpd_query_key_value(buf, "reset", param, sizeof(param)) == ESP_OK) 
            {
                if (!strcmp(param, "true"))
                {
                    res = httpd_resp_send(req, "RESETING...", HTTPD_RESP_USE_STRLEN);
                    esp_restart();
                }
            }
            else
                {
                     /* Check if the query == global vars*/
                    char* eq_ptr=NULL;
                    char eq_pos=0;
                    eq_ptr = strchr(buf, '=');
                    if(eq_ptr)
                    {
                        eq_pos = (int)(eq_ptr - buf);
                        memset(&buffer, 0, 512);
                        strncpy(&buffer[0],buf,eq_pos);
                        for (int x = 0 ; x < no_VariableDefitions ; x++)
                        {
                            if ( ( (GV_Variables[x].flags & GV_READONLY ) ||
                                (GV_Variables[x].flags & GV_WR       )    ) &&
                                (!strcmp(GV_Variables[x].name, buffer)) )
                            {
                                 res = httpd_query_key_value(buf, &buffer[0], param, sizeof(param));
                                 GV_Variables[x].value = atoi(param);
                            } 
                        }
                        ESP_LOGI(TAG, "Vars: %s", buffer);
                        if(res == ESP_FAIL)
                        {
                            res = httpd_resp_send(req, "Invalid attribute", HTTPD_RESP_USE_STRLEN);
                            ESP_LOGI(TAG, "Global vars failed");
                        }
                        else
                        {
                            res = httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN);
                        }
                    }
                }
        }
        free(buf);
    }
    return res;
}

static esp_err_t _get(httpd_req_t *req)
{
    int res = ESP_FAIL;
    char *buf;
    size_t buf_len;
    buf_len = strlen(req->uri) + 1;

    ESP_LOGI(TAG, "GET - Query length %d :%s", buf_len, req->uri);
    if (buf_len > 1) 
    {
        buf = malloc(buf_len);
        /* Query shouldn't contain the = */
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK ) 
            {
                ESP_LOGI(TAG, "GET - Found URL query: %s", buf);
                
                if(!strcmp(buf, "ssids"))
                {
                    memset(&buffer, 0, 512);

                    for (int x = 0 ; x < ap_count ; x++)
                    {
                        strcat(buffer, (char*)&ap_info[x].ssid);
                        if (x != ap_count-1) strcat(buffer,",");
                    }
                    res = httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN);
                    ESP_LOGI(TAG, "ssids: '%s'", buffer);
                }
                if (!strcmp(buf, "ro_vars")) 
                {
                    /* Check readonly global vars */
                    memset(&buffer, 0, 512);

                    for (int x = 0 ; x < no_VariableDefitions ; x++)
                    {
                        if (GV_Variables[x].flags & GV_READONLY )
                        {
                            strcat(buffer, GV_Variables[x].name);
                            if (x != no_VariableDefitions-1) strcat(buffer,",");
                        }
                    }
                    ESP_LOGI(TAG, "ro_vars: '%s'", buffer);
                    res = httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN);
                }

                if (!strcmp(buf, "rw_vars")) 
                {
                    /* Check readonly global vars */
                    memset(&buffer, 0, 512);

                    for (int x = 0 ; x < no_VariableDefitions ; x++)
                    {
                        if (GV_Variables[x].flags & GV_WRITONLY )
                        {
                        strcat(buffer, GV_Variables[x].name);
                        if (x != no_VariableDefitions-1) strcat(buffer,",");
                        }
                    }
                    ESP_LOGI(TAG, "rw_vars: '%s'", buffer);
                    res = httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN);
                }
                else
                {
                    /* Check if the query == global vars*/
                    memset(&buffer, 0, 512);
                    for (int x = 0 ; x < no_VariableDefitions ; x++)
                    {
                        if ( (GV_Variables[x].flags & GV_READONLY) &&
                            (!strcmp(GV_Variables[x].name, buf)) )
                        {
                            char intToChar_buff[20]= {0};
                            snprintf(intToChar_buff, 20, "%d", GV_Variables[x].value);
                            strcat(buffer, intToChar_buff);
                            res = ESP_OK;
                        } 
                    }
                    ESP_LOGI(TAG, "Vars: %s", buffer);
                    if(res == ESP_FAIL)
                    {
                        res = httpd_resp_send(req, "Invalid attribute", HTTPD_RESP_USE_STRLEN);
                        ESP_LOGI(TAG, "Global vars failed");
                    }
                    else
                    {
                        res = httpd_resp_send(req, buffer, HTTPD_RESP_USE_STRLEN);
                    }
                }
        }
        free(buf);
    }
    return res;
}

static esp_err_t read_html(httpd_req_t *req)
{
    return send_html_page(req, 1);
}
static esp_err_t set_html(httpd_req_t *req)
{
    return send_html_page(req, 2);
}
static esp_err_t wifi_manager_html(httpd_req_t *req)
{
    return send_html_page(req, 3);
}


static const httpd_uri_t read_page = {
    .uri       = "/read.html",
    .method    = HTTP_GET,
    .handler   = read_html,
    .user_ctx  = NULL
};

static const httpd_uri_t set_page = {
    .uri       = "/set.html",
    .method    = HTTP_GET,
    .handler   = set_html,
    .user_ctx  = NULL
};

static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = read_html,
    .user_ctx  = NULL
};

static const httpd_uri_t wifi_manager_page = {
    .uri       = "/wifi_manager.html",
    .method    = HTTP_GET,
    .handler   = wifi_manager_html,
    .user_ctx  = NULL
};


static const httpd_uri_t get = {
    .uri       = "/get",
    .method    = HTTP_GET,
    .handler   = _get,
    .user_ctx  = NULL
};

static const httpd_uri_t set = {
    .uri       = "/set",
    .method    = HTTP_GET,
    .handler   = _set,
    .user_ctx  = NULL
};


static esp_err_t _httpGetAsync_js(httpd_req_t *req)
{
    return send_html_page(req, 4);
}

static esp_err_t _update_table_read_js(httpd_req_t *req)
{
    return send_html_page(req, 5);
}

static esp_err_t _update_table_set_js(httpd_req_t *req)
{
    return send_html_page(req, 6);
}

static const httpd_uri_t httpGetAsync_js = {
    .uri       = "/httpGetAsync.js",
    .method    = HTTP_GET,
    .handler   = _httpGetAsync_js,
    .user_ctx  = NULL
};

static const httpd_uri_t update_table_read_js = {
    .uri       = "/update_table_read.js",
    .method    = HTTP_GET,
    .handler   = _update_table_read_js,
    .user_ctx  = NULL
};

static const httpd_uri_t update_table_set_js = {
    .uri       = "/update_table_set.js",
    .method    = HTTP_GET,
    .handler   = _update_table_set_js,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &get);
        httpd_register_uri_handler(server, &set);
        /* HTML */
        httpd_register_uri_handler(server, &wifi_manager_page);
        httpd_register_uri_handler(server, &set_page);
        httpd_register_uri_handler(server, &read_page);
        /* JS */
        httpd_register_uri_handler(server, &update_table_read_js);
        httpd_register_uri_handler(server, &update_table_set_js);
        httpd_register_uri_handler(server, &httpGetAsync_js);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}

void http_server(void)
{
    static httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
    server = start_webserver();
}