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
		},
		{
				"Umiditate",
				GV_READONLY,
				0,
		}
};

const char no_VariableDefitions = sizeof(GV_Variables)/sizeof(GV_Variables[0]);

/* Global objects */
DHT22_TypeDef s1;

/* Callbacks */

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
	if ((pinNumber ==  WaterMeterGPIO_1)||(pinNumber == WaterMeterGPIO_2))
	{

	}

}


void my_app_init(void)
{
	/* Configure adc */
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);


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
	DHT_read(&s1, GPIO_NUM_27);
	GV_Variables[0].value = s1.temperature;
	GV_Variables[1].value = s1.humidity;

	int raw_value = adc1_get_raw(ADC1_CHANNEL_0);
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
