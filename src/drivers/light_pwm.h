#pragma once

#include <stdint.h>
#include "esp_err.h"

// ---------------------------------------------------------
// LIGHT PWM API
// ---------------------------------------------------------
// Single control-path caller; initialize once before setting output.
// Starts at 0%; repeated initialization returns ESP_ERR_INVALID_STATE.
esp_err_t light_pwm_init(void);

// Clamp to 0..100%; returns ESP_ERR_INVALID_STATE before initialization.
esp_err_t light_pwm_set_percent(uint8_t percent);

// Last successful command, initially zero; not hardware feedback.
uint8_t light_pwm_get_percent(void);
