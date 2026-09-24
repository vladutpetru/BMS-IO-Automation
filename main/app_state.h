#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Triggers chapter: 3 triggers, each with min/max in 0..100 and max > min */
#define APP_TRIGGER_COUNT        3
#define APP_TRIGGER_LIMIT_MIN    0
#define APP_TRIGGER_LIMIT_MAX    100
#define APP_TRIGGER_DEFAULT_MIN  20
#define APP_TRIGGER_DEFAULT_MAX  100

/* Boiler chapter: thermostat band in whole degC, 0..80, max > min.
 * Boiler ON below min, OFF at/above max, unchanged in between. */
#define APP_BOIL_LIMIT_MIN    0
#define APP_BOIL_LIMIT_MAX    80
#define APP_BOIL_DEFAULT_MIN  70
#define APP_BOIL_DEFAULT_MAX  80

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
/* Marks the temperature as unavailable (sensor or ADC error) */
void app_state_invalidate_temp(void);
/* Returns false while no valid reading is available */
bool app_state_get_temp_tenths(int *tenths);

/* Boiler min/max (persisted) - uses the same min/max struct as the triggers */
esp_err_t app_state_get_boil(app_trigger_t *out);
esp_err_t app_state_set_boil(int min, int max);        /* ESP_ERR_INVALID_ARG if invalid */

/* Triggers (persisted) */
esp_err_t   app_state_get_trigger(int id, app_trigger_t *out);   /* ESP_ERR_NOT_FOUND for a bad id */
esp_err_t   app_state_set_trigger(int id, int min, int max);     /* ESP_ERR_NOT_FOUND / ESP_ERR_INVALID_ARG */
const char *app_state_trigger_name(int id);                      /* NULL for a bad id */
