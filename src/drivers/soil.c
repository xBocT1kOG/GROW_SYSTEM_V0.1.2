#include "soil.h"
#include "../config/board_config.h"
#include "sensor_math.h"

// ---------------------------------------------------------
// SHARED ADC CHANNEL
// ---------------------------------------------------------
static adc_oneshot_unit_handle_t adc_handle;

esp_err_t soil_init(adc_oneshot_unit_handle_t adc)
{
    if (adc == NULL) return ESP_ERR_INVALID_ARG;
    if (adc_handle != NULL) return ESP_ERR_INVALID_STATE;
    const adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    esp_err_t err = adc_oneshot_config_channel(adc, SOIL_ADC_CHANNEL, &config);
    if (err == ESP_OK) adc_handle = adc;
    return err;
}

// ---------------------------------------------------------
// RAW AND CALIBRATED SAMPLE
// ---------------------------------------------------------
esp_err_t soil_read(int *raw, uint8_t *percent)
{
    if (raw == NULL || percent == NULL) return ESP_ERR_INVALID_ARG;
    if (adc_handle == NULL) return ESP_ERR_INVALID_STATE;
    int sample;
    esp_err_t err = adc_oneshot_read(adc_handle, SOIL_ADC_CHANNEL, &sample);
    if (err != ESP_OK) return err;
    *raw = sample;
    *percent = sensor_adc_percent(sample, SOIL_ADC_MIN_RAW, SOIL_ADC_MAX_RAW);
    return ESP_OK;
}
