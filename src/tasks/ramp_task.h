#pragma once

#include "esp_err.h"

// ---------------------------------------------------------
// PERIODIC PRODUCER STARTUP
// ---------------------------------------------------------
// Call once after app_control_start(), from serialized startup.
esp_err_t ramp_task_start(void);