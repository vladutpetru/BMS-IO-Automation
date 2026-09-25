#ifndef MY_APPLICATION_H
#define MY_APPLICATION_H

#include "driver/gpio.h"

/* Analog temperature sensor: see sensors.h (ADC1 channel 0 = GPIO36)
 * JK BMS on UART2 (GPIO17 TX / GPIO16 RX): see bms.h */

/* Relay / valve action outputs (simple on-off) */
#define ACTION_PIN_1 GPIO_NUM_4
#define ACTION_PIN_2 GPIO_NUM_18
#define ACTION_PIN_3 GPIO_NUM_19
#define ACTION_PIN_4 GPIO_NUM_21
#define ACTION_PIN_COUNT 4

/* Roles of the action outputs */
#define BOILER_PIN     ACTION_PIN_1   /* ON below boiler min temp, OFF at/above boiler max temp */
#define TRIGGER_1_PIN  ACTION_PIN_2   /* ON while battery SoC is inside trigger 1 min..max */
#define TRIGGER_2_PIN  ACTION_PIN_3   /* ON while battery SoC is inside trigger 2 min..max */
#define TRIGGER_3_PIN  ACTION_PIN_4   /* ON while battery SoC is inside trigger 3 min..max */

/* Minimum time between two state changes of the same output */
#define OUTPUT_MIN_DWELL_S  60

/* 1 = output ON when pin is HIGH (active-high relay board).
 * 0 = output ON when pin is LOW  (active-low relay board). */
#define ACTION_ACTIVE_LEVEL 1
#define ACTION_LEVEL(on)    ((on) ? ACTION_ACTIVE_LEVEL : !ACTION_ACTIVE_LEVEL)

/* User application entry */
void my_application(void *arg);
void my_app_init(void);

#endif /* MY_APPLICATION_H */
