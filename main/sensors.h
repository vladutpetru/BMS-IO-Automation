#include "stdio.h"
#include "driver/gpio.h"

typedef struct {
	uint8_t humidity;
	int8_t temperature;
} DHT22_TypeDef;

uint8_t DHT_read(DHT22_TypeDef *dht11, gpio_num_t pin);