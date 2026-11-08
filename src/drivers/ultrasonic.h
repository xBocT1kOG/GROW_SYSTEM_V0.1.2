#pragma once
#include "esp_err.h"

// ---------------------------------------------------------
// HC-SR04 API
// ---------------------------------------------------------
esp_err_t ultrasonic_init(void);
// One bounded measurement; output is unchanged on failure. No filtering here.
esp_err_t ultrasonic_read(float *distance_cm);
