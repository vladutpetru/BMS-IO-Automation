#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_variables.h"
#include "freertos/timers.h"
#include "utils.h"
#include "esp_log.h"
#include "sensors.h"
#include "my_application.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "driver/adc.h"

VariableDefition GV_Variables[] =
{
		{
				"Temperatura",
				GV_READONLY,
				0,
		}
};

const char no_VariableDefitions = sizeof(GV_Variables)/sizeof(GV_Variables[0]);

/* Global objects */
/* DHT22_TypeDef s1;  -- unused now that temperature comes from the analog sensor; uncomment if DHT22 is revived */

/* Callbacks */

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
	if ((pinNumber ==  WaterMeterGPIO_1)||(pinNumber == WaterMeterGPIO_2))
	{

	}

}


static const char *APP_TAG = "MY_APP";

static void init_action_pins(void)
{
    const gpio_num_t action_pins[ACTION_PIN_COUNT] = {
        ACTION_PIN_1, ACTION_PIN_2, ACTION_PIN_3, ACTION_PIN_4
    };

    for (int i = 0; i < ACTION_PIN_COUNT; i++)
    {
        esp_rom_gpio_pad_select_gpio(action_pins[i]);
        esp_err_t err = gpio_set_direction(action_pins[i], GPIO_MODE_OUTPUT);
        if (err != ESP_OK)
        {
            ESP_LOGE(APP_TAG, "Failed to set direction for action pin %d (%s)",
                      action_pins[i], esp_err_to_name(err));
            continue;
        }
        /* Default OFF at boot */
        gpio_set_level(action_pins[i], 0);
    }
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

    err = uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
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
	/* Configure ADC for analog temperature sensor */
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(TEMP_SENSOR_ADC_CHANNEL, ADC_ATTEN_DB_11);

    /* Configure UART2 for the external serial device */
    ESP_ERROR_CHECK(init_uart());

    /* Configure the 4 relay/valve action outputs */
    init_action_pins();

	/* Configure external intrerupt */
	esp_rom_gpio_pad_select_gpio(WaterMeterGPIO_1);
	gpio_set_direction(WaterMeterGPIO_1, GPIO_MODE_INPUT);
	gpio_pulldown_en(WaterMeterGPIO_1);
    gpio_pullup_dis(WaterMeterGPIO_1);
    gpio_set_intr_type(WaterMeterGPIO_1, GPIO_INTR_POSEDGE);

	esp_rom_gpio_pad_select_gpio(WaterMeterGPIO_2);
	gpio_set_direction(WaterMeterGPIO_2, GPIO_MODE_INPUT);
	gpio_pulldown_en(WaterMeterGPIO_2);
    gpio_pullup_dis(WaterMeterGPIO_2);
    gpio_set_intr_type(WaterMeterGPIO_2, GPIO_INTR_POSEDGE);

	gpio_install_isr_service(0);
    gpio_isr_handler_add(WaterMeterGPIO_1, gpio_interrupt_handler, (void *)WaterMeterGPIO_1);
	gpio_isr_handler_add(WaterMeterGPIO_2, gpio_interrupt_handler, (void *)WaterMeterGPIO_2);
}
static void input(void)
{
	GV_Variables[0].value = read_temperature_tenths_c(TEMP_SENSOR_ADC_CHANNEL);
}

static void loop(void)
{
	
}

static void output(void)
{
	
}

void my_application(void *arg)
{
    while(1)
    {
		input();
		loop();
		output();
        vTaskDelay(1000/ portTICK_PERIOD_MS);
    }
}
