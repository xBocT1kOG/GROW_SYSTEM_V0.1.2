#pragma once
#include <stdint.h>
#include "esp_adc/adc_oneshot.h"

// ---------------------------------------------------------
// LDR ADC API
// ---------------------------------------------------------
// Startup passes the same ADC1 handle to both ADC sensor drivers.
esp_err_t ldr_init(adc_oneshot_unit_handle_t adc);
// Single sensor-task caller; outputs remain unchanged on error.
esp_err_t ldr_read(int *raw, uint8_t *percent);
