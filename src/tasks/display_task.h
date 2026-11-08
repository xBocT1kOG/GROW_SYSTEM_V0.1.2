#pragma once

#include "esp_err.h"

// ---------------------------------------------------------
// DISPLAY STARTUP
// ---------------------------------------------------------
// Start once after shared I2C/OLED device setup and application control startup.
// Errors affect only the display; do not abort system startup on this result.
esp_err_t display_task_start(void);
