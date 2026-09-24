#include <stdint.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "sensors.h"

static const char *TAG = "SENSORS";

/* Nominal ESP32 ADC reference, used only if the chip has no calibration in eFuse */
#define DEFAULT_VREF_MV 1100

static adc_oneshot_unit_handle_t s_adc  = NULL;
static adc_cali_handle_t         s_cali = NULL;

esp_err_t sensors_init(void)
{
    if (s_adc != NULL)
    {
        return ESP_OK;
    }

    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = TEMP_SENSOR_ADC_UNIT,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed (%s)", esp_err_to_name(err));
        s_adc = NULL;
        return err;
    }

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = TEMP_SENSOR_ADC_ATTEN,
    };
    err = adc_oneshot_config_channel(s_adc, TEMP_SENSOR_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed (%s)", esp_err_to_name(err));
        goto fail;
    }

    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = TEMP_SENSOR_ADC_UNIT,
        .atten = TEMP_SENSOR_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
#if CONFIG_IDF_TARGET_ESP32
    adc_cali_line_fitting_efuse_val_t efuse = ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF;
    err = adc_cali_scheme_line_fitting_check_efuse(&efuse);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Reading ADC calibration eFuse failed (%s)", esp_err_to_name(err));
        efuse = ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF;
    }
    if (efuse == ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF)
    {
        ESP_LOGW(TAG, "No ADC calibration in eFuse - assuming Vref %d mV, readings may be off by a few degC",
                 DEFAULT_VREF_MV);
        cali_cfg.default_vref = DEFAULT_VREF_MV;
    }
    else
    {
        ESP_LOGI(TAG, "ADC calibration from eFuse (%s)",
                 efuse == ADC_CALI_LINE_FITTING_EFUSE_VAL_EFUSE_TP ? "two-point" : "Vref");
    }
#endif
    err = adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_cali_create_scheme_line_fitting failed (%s)", esp_err_to_name(err));
        s_cali = NULL;
        goto fail;
    }

    ESP_LOGI(TAG, "Temperature sensor ready: ADC1 channel %d (GPIO36), %d samples/reading, offset %d mV",
             TEMP_SENSOR_ADC_CHANNEL, TEMP_SENSOR_SAMPLES, TEMP_SENSOR_OFFSET_MV);
    return ESP_OK;

fail:
    {
        esp_err_t del_err = adc_oneshot_del_unit(s_adc);
        if (del_err != ESP_OK)
        {
            ESP_LOGW(TAG, "adc_oneshot_del_unit failed (%s)", esp_err_to_name(del_err));
        }
        s_adc = NULL;
    }
    return err;
}

int read_temperature_tenths_c(void)
{
    if (s_adc == NULL || s_cali == NULL)
    {
        return SENSOR_TEMP_INVALID;
    }

    /* Average several conversions: the ESP32 ADC has a few LSB of noise per sample */
    int32_t sum = 0;
    for (int i = 0; i < TEMP_SENSOR_SAMPLES; i++)
    {
        int raw;
        esp_err_t err = adc_oneshot_read(s_adc, TEMP_SENSOR_ADC_CHANNEL, &raw);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "adc_oneshot_read failed (%s)", esp_err_to_name(err));
            /* Never return a plausible temperature on error - the control loop would act on it */
            return SENSOR_TEMP_INVALID;
        }
        sum += raw;
    }
    int raw_avg = (int)((sum + TEMP_SENSOR_SAMPLES / 2) / TEMP_SENSOR_SAMPLES);

    /* Calibrated conversion corrects the chip-specific reference voltage and the non-linearity */
    int mv;
    esp_err_t err = adc_cali_raw_to_voltage(s_cali, raw_avg, &mv);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_cali_raw_to_voltage failed (%s)", esp_err_to_name(err));
        return SENSOR_TEMP_INVALID;
    }

    /* 10 mV per degC  ->  1 mV per tenth of a degC */
    return mv - TEMP_SENSOR_OFFSET_MV;
}
