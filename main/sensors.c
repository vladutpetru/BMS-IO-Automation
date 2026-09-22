#include <stdio.h>
#include "esp_log.h"
#include "sensors.h"

static int Dht_Delay(uint32_t timeout, char state, gpio_num_t pin)
{
    int uSec = 0;
    while (gpio_get_level(pin) == state)
    {
        if (uSec > timeout)
            return -1;

        ++uSec;
        esp_rom_delay_us(1); // uSec delay
    }
    return uSec;
}

static uint8_t DHT_start(gpio_num_t pin)
{
    int res;
    /* Config PIN output */
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);

    /* Put line to low */
    gpio_set_level(pin, 0);

    esp_rom_delay_us(3000);

    /* Put line to high*/
    gpio_set_level(pin, 1);

    esp_rom_delay_us(25);

    /* Config pin input */
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    /* DHT will keep the line low for 80 us and then high for 80us */
    res = Dht_Delay(85, 0, pin);
    if (res < 0) return 0;

    /* 80us up */
    res = Dht_Delay(85, 1, pin);
    if (res < 0) return 0;

    return 1;
}

static uint8_t DHT_read_byte(gpio_num_t pin)
{
    uint8_t data = 0, j;
    int res;

    for (j = 0; j < 8; ++j) {
        res = Dht_Delay(100, 0, pin);
        esp_rom_delay_us(40);
        if (gpio_get_level(pin) == 1) {
            data |= (1 << (7 - j));
            res = Dht_Delay(100, 1, pin);
        }
        if (res < 0) return 0;
    }
    return data;
}

int read_temperature_tenths_c(adc1_channel_t channel)
{
    /* 12-bit ADC (0-4095), ADC_ATTEN_DB_11 gives ~0-3.9V full scale on ESP32 */
    int raw = adc1_get_raw(channel);
    if (raw < 0)
    {
        ESP_LOGE("SENSORS", "adc1_get_raw failed for channel %d", channel);
        /* Never return a plausible temperature on error - the control loop would act on it */
        return SENSOR_TEMP_INVALID;
    }

    float voltage = ((float)raw / 4095.0f) * 3.9f;

    /* TMP36-style linear sensor: Vout = 0.5V + 10mV per degC */
    float temp_c = (voltage - 0.5f) * 100.0f;

    return (int)(temp_c * 10.0f);
}

uint8_t DHT_read(DHT22_TypeDef *dht11, gpio_num_t pin)
{
    if (DHT_start(pin)) {

        uint8_t rh_b1 = DHT_read_byte(pin);
        uint8_t rh_b2 = DHT_read_byte(pin);
        uint8_t temp_b1 = DHT_read_byte(pin);
        uint8_t temp_b2 = DHT_read_byte(pin);
        uint8_t sum = DHT_read_byte(pin);
        uint8_t data_sum = (rh_b1 + rh_b2 + temp_b1 + temp_b2) & 0xFF;
        if (sum == data_sum)
        {
            dht11->humidity = ((rh_b1 << 8) | rh_b2) / 10.0;
            dht11->temperature = ((temp_b1 << 8) | temp_b2) / 10.0;
            return 1;
        }
    }
    return 0;
}
