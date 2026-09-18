#include "stdio.h"
#include "driver/gpio.h"
#include "driver/adc.h"

/* --- DHT22 (digital, single-wire) - currently unused, kept for reference --- */
typedef struct {
	uint8_t humidity;
	int8_t temperature;
} DHT22_TypeDef;

uint8_t DHT_read(DHT22_TypeDef *dht11, gpio_num_t pin);

/* --- Analog temperature sensor (TMP36-style: 10 mV/degC, 0.5V offset at 0 degC) --- */
/* Returns temperature in tenths of a degree C (e.g. 235 = 23.5 degC) */
int read_temperature_tenths_c(adc1_channel_t channel);