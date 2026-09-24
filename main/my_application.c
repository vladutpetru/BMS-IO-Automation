#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "sensors.h"
#include "app_state.h"
#include "my_application.h"

static const char *APP_TAG = "MY_APP";

/* ------------------------------------------------------------------ */
/* Control tuning                                                      */
/* ------------------------------------------------------------------ */

/* Triggers: switch ON only once temp is 0.5 C inside [min, max],
 * switch OFF as soon as temp leaves [min, max]. The pin is never ON outside the window. */
#define TRIG_ENTER_MARGIN_TENTHS   5

/* Readings outside this range are treated as a sensor fault -> every output OFF.
 * TMP36 with the input pulled to 0 V (disconnected sensor) reads about -50 C. */
#define TEMP_PLAUSIBLE_MIN_TENTHS  (-400)
#define TEMP_PLAUSIBLE_MAX_TENTHS  1250

enum { OUT_BOILER = 0, OUT_TRIGGER_1, OUT_TRIGGER_2, OUT_TRIGGER_3, OUT_COUNT };

_Static_assert(OUT_COUNT == ACTION_PIN_COUNT, "one output per action pin");
_Static_assert(APP_TRIGGER_COUNT == 3, "outputs are mapped for exactly 3 triggers");

static const gpio_num_t s_out_pins[OUT_COUNT] = {
    BOILER_PIN, TRIGGER_1_PIN, TRIGGER_2_PIN, TRIGGER_3_PIN
};

static bool s_out_want[OUT_COUNT];      /* computed by control_update() */
static bool s_out_applied[OUT_COUNT];   /* last level written; init_action_pins() leaves all OFF */
static bool s_sensor_fault = false;
static int  s_last_temp_tenths = 0;

static const char *out_name(int out)
{
    return (out == OUT_BOILER) ? "Boiler" : app_state_trigger_name(out - OUT_TRIGGER_1);
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

static esp_err_t init_action_pins(void)
{
    esp_err_t err;

    /* Load the OFF level into the output register BEFORE the pins become outputs,
     * so an active-low relay board never sees a short LOW (= ON) pulse at boot. */
    for (int i = 0; i < OUT_COUNT; i++)
    {
        err = gpio_set_level(s_out_pins[i], ACTION_LEVEL(0));
        if (err != ESP_OK)
        {
            ESP_LOGE(APP_TAG, "Presetting GPIO%d OFF failed (%s)", s_out_pins[i], esp_err_to_name(err));
            return err;
        }
        s_out_want[i] = false;
        s_out_applied[i] = false;
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

static esp_err_t init_uart(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "uart_driver_install failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(UART_PORT_NUM, &uart_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "uart_param_config failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "uart_set_pin failed (%s)", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(APP_TAG, "UART2 ready: TX=GPIO%d RX=GPIO%d baud=%d", UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);
    return ESP_OK;
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
        ESP_LOGE(APP_TAG, "Temperature sensor not available - all outputs stay OFF");
    }
    if (init_uart() != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "UART2 not available");
    }
}

/* ------------------------------------------------------------------ */
/* Control logic                                                       */
/* ------------------------------------------------------------------ */

/* Thermostat: ON below min, OFF at/above max, unchanged in between.
 * The min..max band is the hysteresis that stops the relay chattering. */
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

/* Trigger is set while temp is inside [min, max], reset otherwise */
static bool trigger_should_be_set(int temp, int min, int max, bool is_set)
{
    if (is_set)
    {
        return temp >= min && temp <= max;
    }
    return temp >= (min + TRIG_ENTER_MARGIN_TENTHS) && temp <= (max - TRIG_ENTER_MARGIN_TENTHS);
}

/* Decides the wanted state of every output from the boiler temperature,
 * the boiling maximum and the three triggers. Writes nothing to GPIO. */
static void control_update(void)
{
    int temp;
    bool have_temp = app_state_get_temp_tenths(&temp);

    /* Fail-safe: no reading or an impossible one -> everything OFF */
    if (!have_temp || temp < TEMP_PLAUSIBLE_MIN_TENTHS || temp > TEMP_PLAUSIBLE_MAX_TENTHS)
    {
        if (!s_sensor_fault)
        {
            if (have_temp)
            {
                ESP_LOGE(APP_TAG, "Implausible temperature %.1f C - all outputs OFF", temp / 10.0);
            }
            else
            {
                ESP_LOGE(APP_TAG, "No valid temperature reading - all outputs OFF");
            }
            s_sensor_fault = true;
        }
        for (int i = 0; i < OUT_COUNT; i++)
        {
            s_out_want[i] = false;
        }
        return;
    }
    if (s_sensor_fault)
    {
        ESP_LOGI(APP_TAG, "Temperature reading valid again (%.1f C)", temp / 10.0);
        s_sensor_fault = false;
    }
    s_last_temp_tenths = temp;

    /* Boiler: min/max are whole degC, temperature is in tenths */
    app_trigger_t boil;
    esp_err_t berr = app_state_get_boil(&boil);
    if (berr != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "Reading boiler min/max failed (%s) - boiler OFF", esp_err_to_name(berr));
        s_out_want[OUT_BOILER] = false;
    }
    else
    {
        s_out_want[OUT_BOILER] = boiler_should_run(temp, boil.min * 10, boil.max * 10,
                                                   s_out_want[OUT_BOILER]);
    }

    /* Triggers: min/max are whole degC, temperature is in tenths */
    for (int i = 0; i < APP_TRIGGER_COUNT; i++)
    {
        int out = OUT_TRIGGER_1 + i;
        app_trigger_t trg;
        esp_err_t err = app_state_get_trigger(i, &trg);
        if (err != ESP_OK)
        {
            ESP_LOGE(APP_TAG, "Reading trigger %d failed (%s) - output OFF", i, esp_err_to_name(err));
            s_out_want[out] = false;
            continue;
        }
        s_out_want[out] = trigger_should_be_set(temp, trg.min * 10, trg.max * 10, s_out_want[out]);
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
        app_state_invalidate_temp();    /* web page shows "no reading", control goes fail-safe */
    }
    else
    {
        app_state_set_temp_tenths(t);
    }
}

static void loop(void)
{
    control_update();
}

/* Writes only outputs whose wanted state changed; a failed write is retried next cycle */
static void output(void)
{
    for (int i = 0; i < OUT_COUNT; i++)
    {
        if (s_out_want[i] == s_out_applied[i])
        {
            continue;
        }
        esp_err_t err = gpio_set_level(s_out_pins[i], ACTION_LEVEL(s_out_want[i]));
        if (err != ESP_OK)
        {
            ESP_LOGE(APP_TAG, "%s (GPIO%d) write failed (%s)", out_name(i), s_out_pins[i], esp_err_to_name(err));
            continue;
        }
        s_out_applied[i] = s_out_want[i];
        ESP_LOGI(APP_TAG, "%s (GPIO%d) -> %s at %.1f C", out_name(i), s_out_pins[i],
                 s_out_want[i] ? "ON" : "OFF", s_last_temp_tenths / 10.0);
    }
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
