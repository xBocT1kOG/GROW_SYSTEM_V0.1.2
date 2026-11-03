#pragma once

#include <stdbool.h>
#include "esp_err.h"

// ---------------------------------------------------------
// PUMP API
// ---------------------------------------------------------
// Single control-path caller; initialize once before setting output.
// Starts OFF; repeated initialization returns ESP_ERR_INVALID_STATE.
esp_err_t pump_init(void);

// Returns ESP_ERR_INVALID_STATE before initialization.
esp_err_t pump_set(bool enabled);

// Last successful command, initially false; not hardware feedback.
bool pump_get(void);
