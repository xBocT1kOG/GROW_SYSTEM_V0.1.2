#pragma once

#include "freertos/FreeRTOS.h"
#include "app_events.h"

// ---------------------------------------------------------
// CONTROL TASK API
// ---------------------------------------------------------
// Call once from startup before starting event producers; not concurrently.
// Requires successful pump_init() and light_pwm_init(), both initially OFF.
esp_err_t app_control_start(void);

// Task context only; reports invalid data, unavailable queue, or timeout.
esp_err_t app_control_post_event(const app_event_t *event, TickType_t wait_ticks);

// Copy the last accepted snapshot for legacy read-only consumers.
esp_err_t app_control_get_sensors(sensor_snapshot_t *snapshot);
