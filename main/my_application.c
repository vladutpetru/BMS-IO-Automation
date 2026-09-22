#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/uart.h"
#include "sensors.h"
#include "app_state.h"
#include "my_application.h"

static const char *APP_TAG = "MY_APP";

/* ------------------------------------------------------------------ */
/* Control tuning                                                      */
/* ------------------------------------------------------------------ */

/* Boiler: OFF as soon as temp >= max; back ON only when temp < (max - 1.0 C).
 * Stops the relay chattering when the reading hovers around the maximum. */
#define BOIL_RESTART_HYST_TENTHS   10

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

/* Callbacks */
static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    if ((pinNumber == WaterMeterGPIO_1) || (pinNumber == WaterMeterGPIO_2))
    {
        /* Water meter pulse handling - not implemented yet */
    }
}

static const char *out_name(int out)
{
    return (out == OUT_BOILER) ? "Boiler" : app_state_trigger_name(out - OUT_TRIGGER_1);
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

static esp_err_t init_action_pins(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << ACTION_PIN_1) | (1ULL << ACTION_PIN_2) |
                        (1ULL << ACTION_PIN_3) | (1ULL << ACTION_PIN_4),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "Action pin config failed (%s)", esp_err_to_name(err));
        return err;
    }

    for (int i = 0; i < OUT_COUNT; i++)
    {
        /* Everything OFF at boot (respects ACTION_ACTIVE_LEVEL) */
        err = gpio_set_level(s_out_pins[i], ACTION_LEVEL(0));
        if (err != ESP_OK)
        {
            ESP_LOGE(APP_TAG, "Setting GPIO%d OFF failed (%s)", s_out_pins[i], esp_err_to_name(err));
            return err;
        }
        s_out_want[i] = false;
        s_out_applied[i] = false;
    }
    return ESP_OK;
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

static esp_err_t init_water_meter_inputs(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << WaterMeterGPIO_1) | (1ULL << WaterMeterGPIO_2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "Water meter input config failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)   /* INVALID_STATE = already installed */
    {
        ESP_LOGE(APP_TAG, "gpio_install_isr_service failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = gpio_isr_handler_add(WaterMeterGPIO_1, gpio_interrupt_handler, (void *)WaterMeterGPIO_1);
    if (err == ESP_OK)
    {
        err = gpio_isr_handler_add(WaterMeterGPIO_2, gpio_interrupt_handler, (void *)WaterMeterGPIO_2);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "gpio_isr_handler_add failed (%s)", esp_err_to_name(err));
    }
    return err;
}

void my_app_init(void)
{
    /* Analog temperature sensor on ADC1 (GPIO36) */
    esp_err_t err = adc1_config_width(ADC_WIDTH_BIT_12);
    if (err == ESP_OK)
    {
        err = adc1_config_channel_atten(TEMP_SENSOR_ADC_CHANNEL, ADC_ATTEN_DB_11);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "ADC config failed (%s)", esp_err_to_name(err));
    }

    if (init_uart() != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "UART2 not available");
    }
    if (init_action_pins() != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "Action outputs not available");
    }
    if (init_water_meter_inputs() != ESP_OK)
    {
        ESP_LOGE(APP_TAG, "Water meter inputs not available");
    }
}

/* ------------------------------------------------------------------ */
/* Control logic                                                       */
/* ------------------------------------------------------------------ */

/* Boiler runs below the maximum, stops at/above it */
static bool boiler_should_run(int temp, int max, bool running)
{
    if (running)
    {
        return temp < max;
    }
    return temp < (max - BOIL_RESTART_HYST_TENTHS);
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
            if (have_temp && temp != SENSOR_TEMP_INVALID)
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

    /* Boiler */
    s_out_want[OUT_BOILER] = boiler_should_run(temp, app_state_get_boil_max_tenths(),
                                               s_out_want[OUT_BOILER]);

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
    app_state_set_temp_tenths(read_temperature_tenths_c(TEMP_SENSOR_ADC_CHANNEL));
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
