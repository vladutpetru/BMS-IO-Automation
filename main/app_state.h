#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Triggers chapter: 3 triggers on the battery SoC (%), min/max in 20..100, max > min */
#define APP_TRIGGER_COUNT        3
#define APP_TRIGGER_LIMIT_MIN    20
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

/* Output indices, shared by the control task and the HTTP status endpoint */
enum
{
    APP_OUT_BOILER = 0,
    APP_OUT_TRIGGER_1,
    APP_OUT_TRIGGER_2,
    APP_OUT_TRIGGER_3,
    APP_OUT_COUNT
};

typedef struct
{
    bool     on;        /* level currently driven on the pin */
    uint8_t  wait_s;    /* > 0: a change to !on is pending and happens in about this many seconds */
} app_output_status_t;

/* Loads persisted settings from NVS (writes defaults on first boot).
 * Call after nvs_flash_init() and before the HTTP server starts. */
esp_err_t app_state_init(void);

/* Live temperature, written by the application task */
void app_state_set_temp_tenths(int tenths);
void app_state_invalidate_temp(void);                  /* sensor or ADC error */
bool app_state_get_temp_tenths(int *tenths);           /* false while no valid reading */

/* Battery state of charge 0..100 %, written by the BMS task */
void app_state_set_soc(int soc);
void app_state_invalidate_soc(void);
bool app_state_get_soc(int *soc);                      /* false while no valid SoC */

/* Output states published by the control task (APP_OUT_COUNT entries) */
void app_state_set_outputs(const app_output_status_t status[APP_OUT_COUNT]);
void app_state_get_outputs(app_output_status_t status[APP_OUT_COUNT]);

/* Boiler min/max (persisted) */
esp_err_t app_state_get_boil(app_trigger_t *out);
esp_err_t app_state_set_boil(int min, int max);        /* ESP_ERR_INVALID_ARG if invalid */

/* Triggers (persisted) */
esp_err_t   app_state_get_trigger(int id, app_trigger_t *out);   /* ESP_ERR_NOT_FOUND for a bad id */
esp_err_t   app_state_set_trigger(int id, int min, int max);     /* ESP_ERR_NOT_FOUND / ESP_ERR_INVALID_ARG */
const char *app_state_trigger_name(int id);                      /* NULL for a bad id */
