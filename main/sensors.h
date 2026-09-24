#ifndef SENSORS_H
#define SENSORS_H

#include <limits.h>
#include "esp_err.h"
#include "hal/adc_types.h"

/* Analog temperature sensor, 10 mV/degC, on ADC1 channel 0 = GPIO36 (VP) */
#define TEMP_SENSOR_ADC_UNIT      ADC_UNIT_1
#define TEMP_SENSOR_ADC_CHANNEL   ADC_CHANNEL_0     /* GPIO36 */
#define TEMP_SENSOR_ADC_ATTEN     ADC_ATTEN_DB_11   /* ~150..2450 mV usable range */
#define TEMP_SENSOR_SAMPLES       16                /* averaged per reading (noise filter) */

/* Sensor output at 0 degC:
 *   500 mV -> TMP36 style (0.5 V offset)
 *     0 mV -> LM35 style  (no offset)
 * MUST match the fitted sensor - a wrong value shifts every reading by 50 degC. */
#define TEMP_SENSOR_OFFSET_MV     500

/* Returned by read_temperature_tenths_c() when no valid reading is available */
#define SENSOR_TEMP_INVALID INT_MIN

/* Sets up the ADC oneshot driver and calibration. Call once at start-up. */
esp_err_t sensors_init(void);

/* Returns temperature in tenths of a degree C (e.g. 235 = 23.5 degC),
 * or SENSOR_TEMP_INVALID on error / before sensors_init() succeeded */
int read_temperature_tenths_c(void);

#endif /* SENSORS_H */
