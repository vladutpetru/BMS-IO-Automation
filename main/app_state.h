#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Triggers chapter: 3 triggers, each with min/max in 20..100 and max > min */
#define APP_TRIGGER_COUNT        3
#define APP_TRIGGER_LIMIT_MIN    20
#define APP_TRIGGER_LIMIT_MAX    100
#define APP_TRIGGER_DEFAULT_MIN  20
#define APP_TRIGGER_DEFAULT_MAX  100

/* Boiling chapter: max temperature in tenths of degC (0.0 .. 125.0 degC) */
#define APP_BOIL_MAX_MIN_TENTHS     0
#define APP_BOIL_MAX_MAX_TENTHS     1250
#define APP_BOIL_MAX_DEFAULT_TENTHS 800     /* 80.0 degC */

typedef struct
{
    int min;
    int max;
} app_trigger_t;

/* Loads persisted settings from NVS (writes defaults on first boot).
 * Call after nvs_flash_init() and before the HTTP server starts. */
esp_err_t app_state_init(void);

/* Live temperature, written by the application task */
void app_state_set_temp_tenths(int tenths);
/* Returns false until the first reading has been stored */
bool app_state_get_temp_tenths(int *tenths);

/* Boiling max temperature (persisted) */
int       app_state_get_boil_max_tenths(void);
esp_err_t app_state_set_boil_max_tenths(int tenths);   /* ESP_ERR_INVALID_ARG if out of range */

/* Triggers (persisted) */
bool        app_trigger_is_valid(int min, int max);
esp_err_t   app_state_get_trigger(int id, app_trigger_t *out);   /* ESP_ERR_NOT_FOUND for a bad id */
esp_err_t   app_state_set_trigger(int id, int min, int max);     /* ESP_ERR_NOT_FOUND / ESP_ERR_INVALID_ARG */
const char *app_state_trigger_name(int id);                      /* NULL for a bad id */
