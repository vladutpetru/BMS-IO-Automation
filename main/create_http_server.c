#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "global.h"
#include "app_state.h"

static const char *TAG = "HTTP_Server";

#define SPIFFS_BASE_PATH    "/spiffs"
#define SPIFFS_LABEL        "storage"
#define INDEX_FILE_PATH     SPIFFS_BASE_PATH "/index.html"
#define FILE_CHUNK_SIZE     1024
#define MAX_BODY_LEN        256
#define MAX_RECV_RETRIES    3

static httpd_handle_t s_server = NULL;
static bool           s_spiffs_mounted = false;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    esp_err_t err = httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK)
    {
        err = httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set JSON headers (%s)", esp_err_to_name(err));
        return err;
    }
    err = httpd_resp_sendstr(req, json);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send JSON response (%s)", esp_err_to_name(err));
    }
    return err;
}

/* Serialises and sends obj, then frees it (takes ownership) */
static esp_err_t send_cjson(httpd_req_t *req, cJSON *obj)
{
    char *text = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (text == NULL)
    {
        ESP_LOGE(TAG, "cJSON_PrintUnformatted failed (out of memory)");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    esp_err_t err = send_json(req, text);
    cJSON_free(text);
    return err;
}

/* Reads the whole request body and parses it as a JSON object.
 * On failure an error response has already been sent; the caller returns ESP_FAIL,
 * which makes esp_http_server close the socket. */
static esp_err_t read_json_body(httpd_req_t *req, cJSON **out)
{
    char body[MAX_BODY_LEN + 1];
    size_t received = 0;
    int retries = 0;

    *out = NULL;

    if (req->content_len == 0)
    {
        (void)httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request body is empty");
        return ESP_FAIL;
    }
    if (req->content_len > MAX_BODY_LEN)
    {
        (void)httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request body too large");
        return ESP_FAIL;
    }

    while (received < req->content_len)
    {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT && ++retries <= MAX_RECV_RETRIES)
        {
            continue;
        }
        if (ret <= 0)
        {
            ESP_LOGE(TAG, "Receiving body failed (%d)", ret);
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                (void)httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "Malformed JSON body: %s", body);
        (void)httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body must be a JSON object");
        return ESP_FAIL;
    }
    *out = root;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* GET / and /index.html - streamed from SPIFFS in 1 KB chunks          */
/* ------------------------------------------------------------------ */

static esp_err_t index_get_handler(httpd_req_t *req)
{
    if (!s_spiffs_mounted)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Web UI storage is not mounted");
    }

    FILE *f = fopen(INDEX_FILE_PATH, "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "%s not found - was the SPIFFS image flashed?", INDEX_FILE_PATH);
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "index.html missing: flash the SPIFFS image");
    }

    char *chunk = malloc(FILE_CHUNK_SIZE);
    if (chunk == NULL)
    {
        fclose(f);
        ESP_LOGE(TAG, "No memory for file chunk");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }

    esp_err_t err = httpd_resp_set_type(req, "text/html");
    size_t n;
    while (err == ESP_OK && (n = fread(chunk, 1, FILE_CHUNK_SIZE, f)) > 0)
    {
        err = httpd_resp_send_chunk(req, chunk, n);
    }
    if (err == ESP_OK && ferror(f))
    {
        ESP_LOGE(TAG, "Read error on %s", INDEX_FILE_PATH);
        err = ESP_FAIL;
    }
    free(chunk);
    fclose(f);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Sending index.html failed (%s)", esp_err_to_name(err));
        (void)httpd_resp_send_chunk(req, NULL, 0);
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* GET /temperature  ->  {"temp_c": 23.5}                               */
/* ------------------------------------------------------------------ */

static esp_err_t temperature_get_handler(httpd_req_t *req)
{
    int tenths;
    if (!app_state_get_temp_tenths(&tenths))
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No temperature reading yet");
    }
    char json[32];
    snprintf(json, sizeof(json), "{\"temp_c\":%.1f}", tenths / 10.0);
    return send_json(req, json);
}

/* ------------------------------------------------------------------ */
/* GET /api/settings                                                   */
/* {"boil_max_c":100,"triggers":[{"id":0,"name":"Trigger 1","min":20,"max":100},..]} */
/* ------------------------------------------------------------------ */

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *triggers = NULL;

    if (root == NULL ||
        cJSON_AddNumberToObject(root, "boil_max_c", app_state_get_boil_max_tenths() / 10.0) == NULL ||
        (triggers = cJSON_AddArrayToObject(root, "triggers")) == NULL)
    {
        goto oom;
    }

    for (int i = 0; i < APP_TRIGGER_COUNT; i++)
    {
        app_trigger_t t;
        esp_err_t err = app_state_get_trigger(i, &t);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Reading trigger %d failed (%s)", i, esp_err_to_name(err));
            cJSON_Delete(root);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read settings");
        }
        cJSON *item = cJSON_CreateObject();
        if (item == NULL)
        {
            goto oom;
        }
        if (!cJSON_AddItemToArray(triggers, item))
        {
            cJSON_Delete(item);
            goto oom;
        }
        if (cJSON_AddNumberToObject(item, "id", i) == NULL ||
            cJSON_AddStringToObject(item, "name", app_state_trigger_name(i)) == NULL ||
            cJSON_AddNumberToObject(item, "min", t.min) == NULL ||
            cJSON_AddNumberToObject(item, "max", t.max) == NULL)
        {
            goto oom;
        }
    }
    return send_cjson(req, root);

oom:
    cJSON_Delete(root);
    ESP_LOGE(TAG, "Out of memory building settings JSON");
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
}

/* ------------------------------------------------------------------ */
/* POST /api/boil   body {"boil_max_c": 98.5}                           */
/* ------------------------------------------------------------------ */

static esp_err_t boil_post_handler(httpd_req_t *req)
{
    cJSON *root;
    if (read_json_body(req, &root) != ESP_OK)
    {
        return ESP_FAIL;
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "boil_max_c");
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble))
    {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "\"boil_max_c\" must be a number");
    }
    double celsius = item->valuedouble;
    cJSON_Delete(root);

    if (celsius < APP_BOIL_MAX_MIN_TENTHS / 10.0 || celsius > APP_BOIL_MAX_MAX_TENTHS / 10.0)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "\"boil_max_c\" must be between %.1f and %.1f",
                 APP_BOIL_MAX_MIN_TENTHS / 10.0, APP_BOIL_MAX_MAX_TENTHS / 10.0);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
    }

    esp_err_t err = app_state_set_boil_max_tenths((int)lround(celsius * 10.0));
    if (err == ESP_ERR_INVALID_ARG)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Value out of range");
    }
    if (err != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save to flash");
    }

    char json[40];
    snprintf(json, sizeof(json), "{\"boil_max_c\":%.1f}", app_state_get_boil_max_tenths() / 10.0);
    return send_json(req, json);
}

/* ------------------------------------------------------------------ */
/* POST /api/trigger  body {"id": 0, "min": 30, "max": 80}              */
/* Rules: whole numbers, 20 <= min/max <= 100, max > min               */
/* ------------------------------------------------------------------ */

static esp_err_t trigger_post_handler(httpd_req_t *req)
{
    cJSON *root;
    if (read_json_body(req, &root) != ESP_OK)
    {
        return ESP_FAIL;
    }

    const cJSON *id_item  = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *min_item = cJSON_GetObjectItemCaseSensitive(root, "min");
    const cJSON *max_item = cJSON_GetObjectItemCaseSensitive(root, "max");
    if (!cJSON_IsNumber(id_item) || !cJSON_IsNumber(min_item) || !cJSON_IsNumber(max_item))
    {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body needs numeric \"id\", \"min\" and \"max\"");
    }
    double id_d  = id_item->valuedouble;
    double min_d = min_item->valuedouble;
    double max_d = max_item->valuedouble;
    cJSON_Delete(root);

    if (id_d != floor(id_d) || id_d < 0 || id_d >= APP_TRIGGER_COUNT)
    {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown trigger id");
    }
    if (min_d != floor(min_d) || max_d != floor(max_d))
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "\"min\" and \"max\" must be whole numbers");
    }
    if (min_d < APP_TRIGGER_LIMIT_MIN || min_d > APP_TRIGGER_LIMIT_MAX ||
        max_d < APP_TRIGGER_LIMIT_MIN || max_d > APP_TRIGGER_LIMIT_MAX)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "\"min\" and \"max\" must be between %d and %d",
                 APP_TRIGGER_LIMIT_MIN, APP_TRIGGER_LIMIT_MAX);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
    }
    if (max_d <= min_d)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "\"max\" must be greater than \"min\"");
    }

    int id = (int)id_d;
    int min = (int)min_d;
    int max = (int)max_d;
    esp_err_t err = app_state_set_trigger(id, min, max);
    if (err == ESP_ERR_NOT_FOUND)
    {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown trigger id");
    }
    if (err == ESP_ERR_INVALID_ARG)
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid min/max");
    }
    if (err != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save to flash");
    }

    char json[48];
    snprintf(json, sizeof(json), "{\"id\":%d,\"min\":%d,\"max\":%d}", id, min, max);
    return send_json(req, json);
}

/* ------------------------------------------------------------------ */
/* Server setup                                                        */
/* ------------------------------------------------------------------ */

static const httpd_uri_t s_uris[] = {
    { .uri = "/",             .method = HTTP_GET,  .handler = index_get_handler,       .user_ctx = NULL },
    { .uri = "/index.html",   .method = HTTP_GET,  .handler = index_get_handler,       .user_ctx = NULL },
    { .uri = "/temperature",  .method = HTTP_GET,  .handler = temperature_get_handler, .user_ctx = NULL },
    { .uri = "/api/settings", .method = HTTP_GET,  .handler = settings_get_handler,    .user_ctx = NULL },
    { .uri = "/api/boil",     .method = HTTP_POST, .handler = boil_post_handler,       .user_ctx = NULL },
    { .uri = "/api/trigger",  .method = HTTP_POST, .handler = trigger_post_handler,    .user_ctx = NULL },
};
#define URI_COUNT (sizeof(s_uris) / sizeof(s_uris[0]))

static esp_err_t mount_spiffs(void)
{
    /* Mounted once for the lifetime of the app. format_if_mount_failed is false on purpose:
     * formatting would silently erase the flashed web UI. */
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE_PATH,
        .partition_label = SPIFFS_LABEL,
        .max_files = 4,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "SPIFFS mount failed (%s)", esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    err = esp_spiffs_info(SPIFFS_LABEL, &total, &used);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "SPIFFS mounted: %u of %u bytes used", (unsigned)used, (unsigned)total);
    }
    else
    {
        ESP_LOGW(TAG, "esp_spiffs_info failed (%s)", esp_err_to_name(err));
    }

    struct stat st;
    if (stat(INDEX_FILE_PATH, &st) != 0)
    {
        ESP_LOGW(TAG, "%s not found on SPIFFS", INDEX_FILE_PATH);
    }
    return ESP_OK;
}

static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = URI_COUNT + 2;
    config.lru_purge_enable = true;     /* browsers keep sockets open; recycle the oldest */
    config.stack_size = 6144;           /* cJSON parse + body buffer */

    ESP_LOGI(TAG, "Starting server on port %d", config.server_port);
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed (%s)", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    for (size_t i = 0; i < URI_COUNT; i++)
    {
        err = httpd_register_uri_handler(s_server, &s_uris[i]);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Registering %s failed (%s)", s_uris[i].uri, esp_err_to_name(err));
            (void)httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }
    ESP_LOGI(TAG, "Registered %u URI handlers", (unsigned)URI_COUNT);
    return ESP_OK;
}

void http_server(void)
{
    if (s_server != NULL)
    {
        ESP_LOGW(TAG, "HTTP server already running");
        return;
    }

    if (mount_spiffs() == ESP_OK)
    {
        s_spiffs_mounted = true;
    }
    else
    {
        ESP_LOGE(TAG, "Web UI will not be served; the JSON API stays available");
    }

    /* Listens on all interfaces, so it works in both AP and STA mode */
    esp_err_t err = start_webserver();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP server not started (%s)", esp_err_to_name(err));
    }
}
