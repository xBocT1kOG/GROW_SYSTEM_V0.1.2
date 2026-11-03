#include "ramp_task.h"
#include "../app/app_control.h"
#include "../config/app_config.h"
#include "freertos/task.h"
#include "esp_log.h"

// ---------------------------------------------------------
// PERIODIC EVENT PRODUCER
// ---------------------------------------------------------
static TaskHandle_t task_handle;
static const char *TAG = "ramp_task";

static void ramp_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LIGHT_RAMP_INTERVAL_MS));
        const app_event_t event = {
            .type = APP_EVT_LIGHT_RAMP_TICK,
        };
        const esp_err_t err = app_control_post_event(&event, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Event dropped: %s", esp_err_to_name(err));
        }
    }
}

// ---------------------------------------------------------
// STARTUP
// ---------------------------------------------------------
esp_err_t ramp_task_start(void)
{
    if (task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(ramp_task, "ramp_task", PERIODIC_TASK_STACK_BYTES, NULL,
                    PERIODIC_TASK_PRIORITY, &task_handle) != pdPASS) {
        task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}