#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------------------------------------------------------
// BUTTON INPUT API
// ---------------------------------------------------------
typedef enum {
    BUTTON_PUMP = 0,
    BUTTON_AUTO = 1,
    BUTTON_LIGHT_PLUS = 2,
    BUTTON_LIGHT_MINUS = 3,
    BUTTON_COUNT = 4
} button_id_t;

// Exclusive GPIO/service ownership; existing ISR services are rejected.
// Falling edges set bit (1U << button_id) in the task's notification index 0.
esp_err_t buttons_init(TaskHandle_t input_task_handle);

// Task context only; valid after successful initialization.
bool buttons_is_pressed(button_id_t button);
