#pragma once
#include "esp_err.h"

// ---------------------------------------------------------
// SENSOR TASK STARTUP
// ---------------------------------------------------------
// Start once after the four drivers and application queue are ready.
esp_err_t sensor_task_start(void);
