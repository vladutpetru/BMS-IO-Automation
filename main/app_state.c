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
#define NVS_KEY_BOIL_MAX "boil_max"

/* Display names of the triggers - rename them here */
static const char *const s_trigger_names[APP_TRIGGER_COUNT] = { "Trigger 1", "Trigger 2", "Trigger 3" };
/* Each trigger is stored as ONE u32 key (min << 8 | max) so min and max are always written together */
static const char *const s_trigger_keys[APP_TRIGGER_COUNT]  = { "trigger_0", "trigger_1", "trigger_2" };

static SemaphoreHandle_t s_lock = NULL;
static int32_t s_boil_max_tenths = APP_BOIL_MAX_DEFAULT_TENTHS;
static app_trigger_t s_triggers[APP_TRIGGER_COUNT];

/* 32-bit aligned reads/writes are atomic on ESP32; value is stored before the flag */
static volatile int  s_temp_tenths = 0;
static volatile bool s_temp_valid  = false;

static uint32_t pack_trigger(int min, int max)
{
    return ((uint32_t)(min & 0xFF) << 8) | (uint32_t)(max & 0xFF);
}

static void unpack_trigger(uint32_t packed, app_trigger_t *t)
{
    t->min = (int)((packed >> 8) & 0xFF);
    t->max = (int)(packed & 0xFF);
}

bool app_trigger_is_valid(int min, int max)
{
    return min >= APP_TRIGGER_LIMIT_MIN && min <= APP_TRIGGER_LIMIT_MAX &&
           max >= APP_TRIGGER_LIMIT_MIN && max <= APP_TRIGGER_LIMIT_MAX &&
           max > min;
}

static int32_t load_i32(const char *key, int32_t def, int32_t min, int32_t max)
{
    int32_t value = def;
    esp_err_t err = NVS_Read(key, APP_NVS_I32, &value, NULL);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "'%s' not stored yet, writing default %ld", key, (long)def);
        err = NVS_Write(key, APP_NVS_I32, &def, sizeof(def));
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Could not store default for '%s' (%s)", key, esp_err_to_name(err));
        }
        return def;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Reading '%s' failed (%s), using default %ld", key, esp_err_to_name(err), (long)def);
        return def;
    }
    if (value < min || value > max)
    {
        ESP_LOGW(TAG, "'%s'=%ld outside [%ld..%ld], using default %ld",
                 key, (long)value, (long)min, (long)max, (long)def);
        return def;
    }
    return value;
}

static void load_trigger(int id)
{
    const app_trigger_t def = { APP_TRIGGER_DEFAULT_MIN, APP_TRIGGER_DEFAULT_MAX };
    const char *key = s_trigger_keys[id];
    uint32_t packed = 0;

    s_triggers[id] = def;

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
        return;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Reading '%s' failed (%s), using default", key, esp_err_to_name(err));
        return;
    }

    app_trigger_t t;
    unpack_trigger(packed, &t);
    if (!app_trigger_is_valid(t.min, t.max))
    {
        ESP_LOGW(TAG, "'%s' holds invalid %d-%d, using default", key, t.min, t.max);
        return;
    }
    s_triggers[id] = t;
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

    s_boil_max_tenths = load_i32(NVS_KEY_BOIL_MAX, APP_BOIL_MAX_DEFAULT_TENTHS,
                                 APP_BOIL_MAX_MIN_TENTHS, APP_BOIL_MAX_MAX_TENTHS);
    for (int i = 0; i < APP_TRIGGER_COUNT; i++)
    {
        load_trigger(i);
    }

    ESP_LOGI(TAG, "Loaded: boil max %.1f C, triggers %d-%d / %d-%d / %d-%d",
             s_boil_max_tenths / 10.0,
             s_triggers[0].min, s_triggers[0].max,
             s_triggers[1].min, s_triggers[1].max,
             s_triggers[2].min, s_triggers[2].max);
    return ESP_OK;
}

void app_state_set_temp_tenths(int tenths)
{
    s_temp_tenths = tenths;
    s_temp_valid = true;
}

bool app_state_get_temp_tenths(int *tenths)
{
    if (tenths == NULL || !s_temp_valid)
    {
        return false;
    }
    *tenths = s_temp_tenths;
    return true;
}

int app_state_get_boil_max_tenths(void)
{
    int value;
    if (s_lock == NULL)
    {
        return s_boil_max_tenths;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    value = s_boil_max_tenths;
    xSemaphoreGive(s_lock);
    return value;
}

esp_err_t app_state_set_boil_max_tenths(int tenths)
{
    if (s_lock == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (tenths < APP_BOIL_MAX_MIN_TENTHS || tenths > APP_BOIL_MAX_MAX_TENTHS)
    {
        return ESP_ERR_INVALID_ARG;
    }

    int32_t v = tenths;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* Persist first; RAM is only updated if the flash write succeeded */
    esp_err_t err = NVS_Write(NVS_KEY_BOIL_MAX, APP_NVS_I32, &v, sizeof(v));
    if (err == ESP_OK)
    {
        s_boil_max_tenths = v;
    }
    xSemaphoreGive(s_lock);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Boil max set to %.1f C", v / 10.0);
    }
    else
    {
        ESP_LOGE(TAG, "Saving boil max failed (%s)", esp_err_to_name(err));
    }
    return err;
}

esp_err_t app_state_get_trigger(int id, app_trigger_t *out)
{
    if (id < 0 || id >= APP_TRIGGER_COUNT || out == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_lock == NULL)
    {
        *out = s_triggers[id];
        return ESP_OK;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_triggers[id];
    xSemaphoreGive(s_lock);
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

    uint32_t packed = pack_trigger(min, max);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = NVS_Write(s_trigger_keys[id], APP_NVS_U32, &packed, sizeof(packed));
    if (err == ESP_OK)
    {
        s_triggers[id].min = min;
        s_triggers[id].max = max;
    }
    xSemaphoreGive(s_lock);

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
