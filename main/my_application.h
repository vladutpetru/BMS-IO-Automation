/* Defines */
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/uart.h"

/* Water meter pulse-counter inputs (unchanged, unrelated to this pin plan) */
#define WaterMeterGPIO_1 GPIO_NUM_2
#define WaterMeterGPIO_2 GPIO_NUM_15

/* Analog temperature sensor (ADC1 - safe to read with Wi-Fi active) */
#define TEMP_SENSOR_ADC_CHANNEL ADC1_CHANNEL_0   /* GPIO36 */

/* UART2 - external serial device */
#define UART_PORT_NUM   UART_NUM_2
#define UART_TX_PIN     GPIO_NUM_17
#define UART_RX_PIN     GPIO_NUM_16
#define UART_BAUD_RATE  115200
#define UART_BUF_SIZE   1024

/* Relay / valve action outputs (simple on-off) */
#define ACTION_PIN_1 GPIO_NUM_4
#define ACTION_PIN_2 GPIO_NUM_18
#define ACTION_PIN_3 GPIO_NUM_19
#define ACTION_PIN_4 GPIO_NUM_21
#define ACTION_PIN_COUNT 4

#define ACTION_ON(pin)  gpio_set_level((pin), 1)
#define ACTION_OFF(pin) gpio_set_level((pin), 0)

/* User application entry */
void my_application(void *arg);
void my_app_init(void);