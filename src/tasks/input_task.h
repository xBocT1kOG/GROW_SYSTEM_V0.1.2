#pragma once

#include "esp_err.h"

// ---------------------------------------------------------
// INPUT TASK STARTUP
// ---------------------------------------------------------
// Call once from startup after app_control_start(), without concurrent callers.
// Start once; this task owns all four physical button inputs.
// Queue-full events are dropped with a warning; held buttons never auto-repeat.
esp_err_t input_task_start(void);
