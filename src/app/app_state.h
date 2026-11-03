#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// ---------------------------------------------------------
// APPLICATION SNAPSHOTS
// ---------------------------------------------------------
typedef enum {
    LIGHT_MOD_IDLE,
    LIGHT_MOD_DAWN,
    LIGHT_MOD_DUSK
} light_mod_t;

typedef struct {
    uint8_t system_level_percent; // Commanded output: 0..100
    uint8_t auto_target_percent;
    uint8_t actual_percent;       // LDR telemetry only; never drives PWM
    bool auto_enabled;
    light_mod_t mod;
} light_state_t;

typedef struct {
    float temperature_c;
    float humidity_percent;
    int soil_raw;                // Preserve raw samples for calibration
    uint8_t soil_percent;
    float water_distance_cm;
    uint8_t water_percent;
    int ldr_raw;
    uint8_t light_actual_percent;
} sensor_snapshot_t;

typedef struct {
    light_state_t light;
    sensor_snapshot_t sensors;
    bool pump_enabled;
    bool wifi_connected;
    bool time_synced;
    esp_err_t last_error;
} app_state_t;
