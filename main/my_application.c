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

/* Callbacks */
static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    if ((pinNumber == WaterMeterGPIO_1) || (pinNumber == WaterMeterGPIO_2))
    {
        /* Water meter pulse handling - not implemented yet */
    }
}

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

    const gpio_num_t pins[ACTION_PIN_COUNT] = { ACTION_PIN_1, ACTION_PIN_2, ACTION_PIN_3, ACTION_PIN_4 };
    for (int i = 0; i < ACTION_PIN_COUNT; i++)
    {
        /* Default OFF at boot */
        err = gpio_set_level(pins[i], 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(APP_TAG, "Setting action pin %d low failed (%s)", pins[i], esp_err_to_name(err));
            return err;
        }
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

static void input(void)
{
    app_state_set_temp_tenths(read_temperature_tenths_c(TEMP_SENSOR_ADC_CHANNEL));
}

static void loop(void)
{
    /* Control logic goes here. Current values are available as:
     *   int t;  app_state_get_temp_tenths(&t);
     *   app_state_get_boil_max_tenths();
     *   app_trigger_t trg;  app_state_get_trigger(0..2, &trg);   (trg.min / trg.max)
     */
}

static void output(void)
{
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
