#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "app_nvs.h"
#include "app_state.h"

static const char *TAG = "APP_STATE";

/* NVS keys must be <= 15 characters */
#define NVS_KEY_BOILER "boiler"   /* packed min/max; the old "boil_max" key is no longer used */

/* Display names of the triggers - rename them here */
static const char *const s_trigger_names[APP_TRIGGER_COUNT] = { "Trigger 1", "Trigger 2", "Trigger 3" };
/* Each trigger is stored as ONE u32 key (min << 8 | max) so min and max are always written together */
static const char *const s_trigger_keys[APP_TRIGGER_COUNT]  = { "trigger_0", "trigger_1", "trigger_2" };

static SemaphoreHandle_t s_lock = NULL;
static app_trigger_t s_boiler = { APP_BOIL_DEFAULT_MIN, APP_BOIL_DEFAULT_MAX };
static app_trigger_t s_triggers[APP_TRIGGER_COUNT];

/* Value and validity live in ONE aligned 32-bit word, which the ESP32 reads and writes
 * atomically: a reader can never see a new "valid" flag together with a stale value. */
#define TEMP_NONE INT_MIN
static volatile int s_temp_tenths = TEMP_NONE;

static uint32_t pack_trigger(int min, int max)
{
    return ((uint32_t)(min & 0xFF) << 8) | (uint32_t)(max & 0xFF);
}

static void unpack_trigger(uint32_t packed, app_trigger_t *t)
{
    t->min = (int)((packed >> 8) & 0xFF);
    t->max = (int)(packed & 0xFF);
}

static bool app_trigger_is_valid(int min, int max)
{
    return min >= APP_TRIGGER_LIMIT_MIN && min <= APP_TRIGGER_LIMIT_MAX &&
           max >= APP_TRIGGER_LIMIT_MIN && max <= APP_TRIGGER_LIMIT_MAX &&
           max > min;
}

static bool app_boil_is_valid(int min, int max)
{
    return min >= APP_BOIL_LIMIT_MIN && min <= APP_BOIL_LIMIT_MAX &&
           max >= APP_BOIL_LIMIT_MIN && max <= APP_BOIL_LIMIT_MAX &&
           max > min;
}

/* Loads a packed min/max pair; a missing key gets the default written, an invalid one falls back */
static app_trigger_t load_pair(const char *key, app_trigger_t def, bool (*is_valid)(int, int))
{
    uint32_t packed = 0;
    esp_err_t err = NVS_Read(key, APP_NVS_U32, &packed, NULL);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "'%s' not stored yet, writing default %d-%d", key, def.min, def.max);
        packed = pack_trigger(def.min, def.max);
        err = NVS_Write(key, APP_NVS_U32, &packed, sizeof(packed));
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Could not store default for '%s' (%s)", key, esp_err_to_name(err));
        }
        return def;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Reading '%s' failed (%s), using default", key, esp_err_to_name(err));
        return def;
    }

    app_trigger_t t;
    unpack_trigger(packed, &t);
    if (!is_valid(t.min, t.max))
    {
        ESP_LOGW(TAG, "'%s' holds invalid %d-%d, using default", key, t.min, t.max);
        return def;
    }
    return t;
}

/* Persist first; RAM is only updated if the flash write succeeded, so both always agree */
static esp_err_t save_pair(const char *key, app_trigger_t *dst, int min, int max)
{
    uint32_t packed = pack_trigger(min, max);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = NVS_Write(key, APP_NVS_U32, &packed, sizeof(packed));
    if (err == ESP_OK)
    {
        dst->min = min;
        dst->max = max;
    }
    xSemaphoreGive(s_lock);
    return err;
}

static void read_pair(const app_trigger_t *src, app_trigger_t *out)
{
    if (s_lock == NULL)
    {
        *out = *src;
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = *src;
    xSemaphoreGive(s_lock);
}

esp_err_t app_state_init(void)
{
    if (s_lock != NULL)
    {
        return ESP_OK;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL)
    {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }

    const app_trigger_t boil_def = { APP_BOIL_DEFAULT_MIN, APP_BOIL_DEFAULT_MAX };
    s_boiler = load_pair(NVS_KEY_BOILER, boil_def, app_boil_is_valid);

    const app_trigger_t trig_def = { APP_TRIGGER_DEFAULT_MIN, APP_TRIGGER_DEFAULT_MAX };
    for (int i = 0; i < APP_TRIGGER_COUNT; i++)
    {
        s_triggers[i] = load_pair(s_trigger_keys[i], trig_def, app_trigger_is_valid);
    }

    ESP_LOGI(TAG, "Loaded: boiler %d-%d C, triggers %d-%d / %d-%d / %d-%d",
             s_boiler.min, s_boiler.max,
             s_triggers[0].min, s_triggers[0].max,
             s_triggers[1].min, s_triggers[1].max,
             s_triggers[2].min, s_triggers[2].max);
    return ESP_OK;
}

void app_state_set_temp_tenths(int tenths)
{
    s_temp_tenths = tenths;
}

void app_state_invalidate_temp(void)
{
    s_temp_tenths = TEMP_NONE;
}

bool app_state_get_temp_tenths(int *tenths)
{
    int value = s_temp_tenths;      /* single read of the shared word */
    if (tenths == NULL || value == TEMP_NONE)
    {
        return false;
    }
    *tenths = value;
    return true;
}

esp_err_t app_state_get_boil(app_trigger_t *out)
{
    if (out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    read_pair(&s_boiler, out);
    return ESP_OK;
}

esp_err_t app_state_set_boil(int min, int max)
{
    if (s_lock == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!app_boil_is_valid(min, max))
    {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = save_pair(NVS_KEY_BOILER, &s_boiler, min, max);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Boiler set to %d-%d C", min, max);
    }
    else
    {
        ESP_LOGE(TAG, "Saving boiler min/max failed (%s)", esp_err_to_name(err));
    }
    return err;
}

esp_err_t app_state_get_trigger(int id, app_trigger_t *out)
{
    if (id < 0 || id >= APP_TRIGGER_COUNT || out == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }
    read_pair(&s_triggers[id], out);
    return ESP_OK;
}

esp_err_t app_state_set_trigger(int id, int min, int max)
{
    if (s_lock == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (id < 0 || id >= APP_TRIGGER_COUNT)
    {
        return ESP_ERR_NOT_FOUND;
    }
    if (!app_trigger_is_valid(min, max))
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = save_pair(s_trigger_keys[id], &s_triggers[id], min, max);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "%s set to %d-%d", s_trigger_names[id], min, max);
    }
    else
    {
        ESP_LOGE(TAG, "Saving %s failed (%s)", s_trigger_names[id], esp_err_to_name(err));
    }
    return err;
}

const char *app_state_trigger_name(int id)
{
    if (id < 0 || id >= APP_TRIGGER_COUNT)
    {
        return NULL;
    }
    return s_trigger_names[id];
}
