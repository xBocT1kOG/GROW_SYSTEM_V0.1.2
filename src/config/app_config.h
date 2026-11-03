#pragma once

// ---------------------------------------------------------
// TASK PERIODS
// ---------------------------------------------------------
#define SENSOR_PERIOD_MS              2000
#define DISPLAY_PERIOD_MS             2000
#define BLYNK_TELEMETRY_PERIOD_MS      5000
#define BLYNK_CONTROL_POLL_MS          5000
#define TIME_CHECK_PERIOD_MS         60000 // Schedule checks: one minute
#define LIGHT_RAMP_INTERVAL_MS      600000 // Ramp ticks: ten minutes

// ---------------------------------------------------------
// LIGHT SCHEDULE
// ---------------------------------------------------------
#define LIGHT_STEP_PERCENT               5
#define LIGHT_DAWN_START_HOUR             6
#define LIGHT_DUSK_START_HOUR            20
#define LIGHT_AUTO_TARGET_PERCENT       60

// ---------------------------------------------------------
// SENSOR CALIBRATION
// ---------------------------------------------------------
#define SOIL_ADC_MIN_RAW               500 // Raw calibration endpoints
#define SOIL_ADC_MAX_RAW              3800
#define LDR_ADC_MIN_RAW                500
#define LDR_ADC_MAX_RAW               3800
#define WATER_DISTANCE_EMPTY_CM      16.0f // Distance from sensor to water
#define WATER_DISTANCE_FULL_CM        7.0f

// ---------------------------------------------------------
// CONTROL TASK RESOURCES
// ---------------------------------------------------------
#define APP_EVENT_QUEUE_LENGTH          16
#define CONTROL_TASK_STACK_BYTES      4096 // ESP-IDF stack size is in bytes
#define CONTROL_TASK_PRIORITY            5

// ---------------------------------------------------------
// BUTTON INPUT TIMING AND RESOURCES
// ---------------------------------------------------------
#define BUTTON_DEBOUNCE_MS              40 // Stable press and release interval
#define BUTTON_SAMPLE_PERIOD_MS         10 // Sample while a button is active
#define INPUT_TASK_STACK_BYTES        3072
#define INPUT_TASK_PRIORITY              5

// Time and ramp tasks only publish events.
#define PERIODIC_TASK_STACK_BYTES     3072
#define PERIODIC_TASK_PRIORITY           4
