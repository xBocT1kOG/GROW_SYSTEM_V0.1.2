#pragma once

#include <time.h>
#include "app_state.h"

// ---------------------------------------------------------
// CONTROL QUEUE MESSAGES
// ---------------------------------------------------------
typedef enum {
    APP_EVT_PUMP_TOGGLE,
    APP_EVT_LIGHT_INCREMENT,
    APP_EVT_LIGHT_DECREMENT,
    APP_EVT_LIGHT_AUTO_TOGGLE,
    APP_EVT_BLYNK_PUMP_SET,
    APP_EVT_BLYNK_LIGHT_SET,
    APP_EVT_BLYNK_AUTO_SET,
    APP_EVT_SENSOR_UPDATE,
    APP_EVT_TIME_CHECK,
    APP_EVT_LIGHT_RAMP_TICK,
    APP_EVT_WIFI_CONNECTED,
    APP_EVT_WIFI_DISCONNECTED,
    APP_EVT_TIME_SYNCED,
    APP_EVT_SYSTEM_ERROR,
    APP_EVT_COUNT
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    union {
        bool enabled;              // Blynk pump/auto set
        uint8_t light_percent;     // Blynk light set: 0..100
        sensor_snapshot_t sensors; // Copied by value; no producer-owned pointers
        time_t timestamp;          // Time check: epoch seconds, after sync
        esp_err_t error;           // System error
    } data;
} app_event_t;
