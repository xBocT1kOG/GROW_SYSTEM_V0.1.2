#include "../diagnostics/diagnostics.h"
#include "sensor_task.h"
#include "sensor_filter.h"
#include "../app/app_control.h"
#include "../config/app_config.h"
#include "../drivers/htu21.h"
#include "../drivers/soil.h"
#include "../drivers/ldr.h"
#include "../drivers/ultrasonic.h"
#include "../drivers/sensor_math.h"
#include "freertos/task.h"
#include "esp_log.h"

// ---------------------------------------------------------
// SENSOR ACQUISITION
// ---------------------------------------------------------
static TaskHandle_t task_handle;
static const char *TAG = "sensor_task";

// Report failures and recovery once per status change.
static void report_status(const char *sensor, esp_err_t previous, esp_err_t current)
{
    if (previous == current) return;
    if (current == ESP_OK) {
        ESP_LOGI(TAG, "%s recovered", sensor);
    } else {
        ESP_LOGW(TAG, "%s read failed: %s", sensor, esp_err_to_name(current));
    }
}

static void sensor_task(void *argument)
{
    (void)argument;
    diagnostics_sample_stack(DIAG_TASK_sensor);
    sensor_snapshot_t snapshot = {
        .temperature_c = NAN, .humidity_percent = NAN,
        .soil_raw = -1, .ldr_raw = -1, .water_distance_cm = NAN,
    };
    water_filter_t water_filter = {0};
    TickType_t last_wake = xTaskGetTickCount();
    int64_t diagnostic_previous = -1;
    for (;;) {
        diagnostics_sample_stack(DIAG_TASK_sensor);
        diagnostics_check_deadline(&diagnostic_previous, SENSOR_PERIOD_MS);
        diagnostics_increment(DIAG_sensor_cycles);
        esp_err_t err = htu21_read(&snapshot.temperature_c, &snapshot.humidity_percent);
        report_status("HTU21", snapshot.htu21_error, err);
        if (err != ESP_OK) diagnostics_increment(DIAG_htu21_errors);
        snapshot.htu21_error = err;
        if (err == ESP_OK) snapshot.htu21_valid = true;

        err = soil_read(&snapshot.soil_raw, &snapshot.soil_percent);
        report_status("Soil", snapshot.soil_error, err);
        if (err != ESP_OK) diagnostics_increment(DIAG_soil_errors);
        snapshot.soil_error = err;
        if (err == ESP_OK) snapshot.soil_valid = true;

        err = ldr_read(&snapshot.ldr_raw, &snapshot.light_actual_percent);
        report_status("LDR", snapshot.ldr_error, err);
        if (err != ESP_OK) diagnostics_increment(DIAG_ldr_errors);
        snapshot.ldr_error = err;
        if (err == ESP_OK) snapshot.ldr_valid = true;

        float distance;
        err = ultrasonic_read(&distance); // Exactly one trigger per cycle.
        if (err == ESP_OK) {
            if (water_filter_add(&water_filter, distance, &snapshot.water_distance_cm)) {
                snapshot.water_percent = sensor_water_percent(snapshot.water_distance_cm);
                snapshot.water_valid = true;
            } else {
                err = ESP_ERR_INVALID_RESPONSE;
            }
        }
        report_status("HC-SR04", snapshot.water_error, err);
        if (err != ESP_OK) diagnostics_increment(DIAG_ultrasonic_errors);
        if (err == ESP_ERR_TIMEOUT) diagnostics_increment(DIAG_ultrasonic_timeouts);
        snapshot.water_error = err;

        const app_event_t event = { .type = APP_EVT_SENSOR_UPDATE, .data.sensors = snapshot };
        err = app_control_post_event(&event, 0);
        if (err != ESP_OK) ESP_LOGW(TAG, "Snapshot dropped: %s", esp_err_to_name(err));
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

// ---------------------------------------------------------
// STARTUP
// ---------------------------------------------------------
esp_err_t sensor_task_start(void)
{
    if (task_handle != NULL) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK_BYTES, NULL,
                    SENSOR_TASK_PRIORITY, &task_handle) != pdPASS) {
        task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
