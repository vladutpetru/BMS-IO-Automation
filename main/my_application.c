#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sensors.h"
#include "app_state.h"
#include "my_application.h"

static const char *APP_TAG = "MY_APP";

/* ------------------------------------------------------------------ */
/* Control tuning                                                      */
/* ------------------------------------------------------------------ */

/* Readings outside this range are treated as a sensor fault -> boiler OFF.
 * TMP36 with the input pulled to 0 V (disconnected sensor) reads about -50 C. */
#define TEMP_PLAUSIBLE_MIN_TENTHS  (-400)
#define TEMP_PLAUSIBLE_MAX_TENTHS  1250

#define DWELL_TICKS  pdMS_TO_TICKS(OUTPUT_MIN_DWELL_S * 1000)

_Static_assert(APP_OUT_COUNT == ACTION_PIN_COUNT, "one output per action pin");
_Static_assert(APP_TRIGGER_COUNT == 3, "outputs are mapped for exactly 3 triggers");

static const gpio_num_t s_out_pins[APP_OUT_COUNT] = {
    BOILER_PIN, TRIGGER_1_PIN, TRIGGER_2_PIN, TRIGGER_3_PIN
};

static bool       s_out_want[APP_OUT_COUNT];       /* decided by control_update() */
static bool       s_out_applied[APP_OUT_COUNT];    /* level actually on the pin */
static bool       s_out_off_now[APP_OUT_COUNT];    /* safety: switch OFF without waiting the dwell time */
static TickType_t s_last_change[APP_OUT_COUNT];    /* tick of the last pin change */

static bool s_temp_fault = false;
static bool s_soc_fault = false;
static int  s_last_temp_tenths = 0;
static int  s_last_soc = 0;

static const char *out_name(int out)
{
    return (out == APP_OUT_BOILER) ? "Boiler" : app_state_trigger_name(out - APP_OUT_TRIGGER_1);
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

static esp_err_t init_action_pins(void)
{
    esp_err_t err;
    TickType_t now = xTaskGetTickCount();

    /* Load the OFF level into the output register BEFORE the pins become outputs,
     * so an active-low relay board never sees a short LOW (= ON) pulse at boot. */
    for (int i = 0; i < APP_OUT_COUNT; i++)
    {
        err = gpio_set_level(s_out_pins[i], ACTION_LEVEL(0));
        if (err != ESP_OK)
        {
            ESP_LOGE(APP_TAG, "Presetting GPIO%d OFF failed (%s)", s_out_pins[i], esp_err_to_name(err));
            return err;
        }
        s_out_want[i] = false;
        s_out_applied[i] = false;
        s_out_off_now[i] = false;
        /* The dwell time also runs from boot: a board stuck in a reset loop can never
         * switch a load faster than once a minute. */
        s_last_change[i] = now;
    }

    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << ACTION_PIN_1) | (1ULL << ACTION_PIN_2) |
                        (1ULL << ACTION_PIN_3) | (1ULL << ACTION_PIN_4),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "Action pin config failed (%s)", esp_err_to_name(err));
    }
    return err;
}

void my_app_init(void)
{
    /* Outputs first, so the relays are OFF as early as possible */
    if (init_action_pins() != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "Action outputs not available");
    }
    if (sensors_init() != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "Temperature sensor not available - boiler stays OFF");
    }
}

/* ------------------------------------------------------------------ */
/* Control logic                                                       */
/* ------------------------------------------------------------------ */

/* Thermostat: ON below min, OFF at/above max, unchanged in between */
static bool boiler_should_run(int temp, int min, int max, bool running)
{
    if (temp >= max)
    {
        return false;
    }
    if (temp < min)
    {
        return true;
    }
    return running;
}

static void control_boiler(void)
{
    int temp;
    bool have_temp = app_state_get_temp_tenths(&temp);

    /* Fail-safe: no reading or an impossible one -> boiler OFF at once */
    if (!have_temp || temp < TEMP_PLAUSIBLE_MIN_TENTHS || temp > TEMP_PLAUSIBLE_MAX_TENTHS)
    {
        if (!s_temp_fault)
        {
            if (have_temp)
            {
                ESP_LOGE(APP_TAG, "Implausible temperature %.1f C - boiler OFF", temp / 10.0);
            }
            else
            {
                ESP_LOGE(APP_TAG, "No valid temperature reading - boiler OFF");
            }
            s_temp_fault = true;
        }
        s_out_want[APP_OUT_BOILER] = false;
        s_out_off_now[APP_OUT_BOILER] = true;
        return;
    }
    if (s_temp_fault)
    {
        ESP_LOGI(APP_TAG, "Temperature reading valid again (%.1f C)", temp / 10.0);
        s_temp_fault = false;
    }
    s_last_temp_tenths = temp;

    app_trigger_t boil;
    esp_err_t err = app_state_get_boil(&boil);
    if (err != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "Reading boiler min/max failed (%s) - boiler OFF", esp_err_to_name(err));
        s_out_want[APP_OUT_BOILER] = false;
        s_out_off_now[APP_OUT_BOILER] = true;
        return;
    }

    /* min/max are whole degC, temperature is in tenths */
    s_out_want[APP_OUT_BOILER] = boiler_should_run(temp, boil.min * 10, boil.max * 10,
                                                   s_out_want[APP_OUT_BOILER]);
    /* Overheat protection: at/above max the boiler goes OFF without waiting the dwell time */
    s_out_off_now[APP_OUT_BOILER] = (temp >= boil.max * 10);
}

static void control_triggers(void)
{
    int soc;
    if (!app_state_get_soc(&soc))
    {
        /* No battery data: loads OFF at once rather than drain a battery we cannot see */
        if (!s_soc_fault)
        {
            ESP_LOGE(APP_TAG, "No battery SoC from the BMS - triggers OFF");
            s_soc_fault = true;
        }
        for (int i = 0; i < APP_TRIGGER_COUNT; i++)
        {
            s_out_want[APP_OUT_TRIGGER_1 + i] = false;
            s_out_off_now[APP_OUT_TRIGGER_1 + i] = true;
        }
        return;
    }
    if (s_soc_fault)
    {
        ESP_LOGI(APP_TAG, "Battery SoC available again (%d %%)", soc);
        s_soc_fault = false;
    }
    s_last_soc = soc;

    for (int i = 0; i < APP_TRIGGER_COUNT; i++)
    {
        int out = APP_OUT_TRIGGER_1 + i;
        app_trigger_t trg;
        esp_err_t err = app_state_get_trigger(i, &trg);
        if (err != ESP_OK)
        {
            ESP_LOGE(APP_TAG, "Reading trigger %d failed (%s) - output OFF", i, esp_err_to_name(err));
            s_out_want[out] = false;
            s_out_off_now[out] = true;
            continue;
        }
        /* Set while SoC is inside [min, max]; the 1-minute dwell time is the hysteresis */
        s_out_want[out] = (soc >= trg.min && soc <= trg.max);
        s_out_off_now[out] = false;
    }
}

/* ------------------------------------------------------------------ */
/* Task                                                                */
/* ------------------------------------------------------------------ */

static void input(void)
{
    int t = read_temperature_tenths_c();
    if (t == SENSOR_TEMP_INVALID)
    {
        app_state_invalidate_temp();    /* web page shows "no reading", boiler goes fail-safe */
    }
    else
    {
        app_state_set_temp_tenths(t);
    }
    /* The battery SoC is written by the BMS task (bms.c) */
}

static void loop(void)
{
    control_boiler();
    control_triggers();
}

/* Applies the wanted states, at most one change per output per OUTPUT_MIN_DWELL_S,
 * except safety switch-offs. Publishes the result for the web page. */
static void output(void)
{
    TickType_t now = xTaskGetTickCount();
    app_output_status_t status[APP_OUT_COUNT];

    for (int i = 0; i < APP_OUT_COUNT; i++)
    {
        TickType_t since = now - s_last_change[i];      /* unsigned: correct across tick wrap */
        bool dwell_done = since >= DWELL_TICKS;
        bool safety_off = !s_out_want[i] && s_out_off_now[i];

        if (s_out_want[i] != s_out_applied[i] && (dwell_done || safety_off))
        {
            esp_err_t err = gpio_set_level(s_out_pins[i], ACTION_LEVEL(s_out_want[i]));
            if (err != ESP_OK)
            {
                ESP_LOGE(APP_TAG, "%s (GPIO%d) write failed (%s)", out_name(i), s_out_pins[i],
                         esp_err_to_name(err));
            }
            else
            {
                s_out_applied[i] = s_out_want[i];
                s_last_change[i] = now;
                since = 0;
                if (i == APP_OUT_BOILER)
                {
                    ESP_LOGI(APP_TAG, "%s (GPIO%d) -> %s at %.1f C%s", out_name(i), s_out_pins[i],
                             s_out_applied[i] ? "ON" : "OFF", s_last_temp_tenths / 10.0,
                             (safety_off && !dwell_done) ? " (immediate)" : "");
                }
                else
                {
                    ESP_LOGI(APP_TAG, "%s (GPIO%d) -> %s at SoC %d %%%s", out_name(i), s_out_pins[i],
                             s_out_applied[i] ? "ON" : "OFF", s_last_soc,
                             (safety_off && !dwell_done) ? " (immediate)" : "");
                }
            }
        }

        status[i].on = s_out_applied[i];
        status[i].wait_s = 0;
        if (s_out_want[i] != s_out_applied[i])
        {
            TickType_t left = (since >= DWELL_TICKS) ? 0 : DWELL_TICKS - since;
            uint32_t left_s = (pdTICKS_TO_MS(left) + 999) / 1000;
            status[i].wait_s = (uint8_t)(left_s == 0 ? 1 : left_s);
        }
    }
    app_state_set_outputs(status);
}

void my_application(void *arg)
{
    (void)arg;
    while (1)
    {
        input();
        loop();
        output();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
