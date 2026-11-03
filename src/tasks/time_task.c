#include "time_task.h"
#include "../app/app_control.h"
#include "../config/app_config.h"
#include "freertos/task.h"
#include "esp_log.h"

// ---------------------------------------------------------
// PERIODIC EVENT PRODUCER
// ---------------------------------------------------------
static TaskHandle_t task_handle;
static const char *TAG = "time_task";

static void time_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TIME_CHECK_PERIOD_MS));
        const app_event_t event = {
            .type = APP_EVT_TIME_CHECK,
            .data.time = time_service_now(),
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
esp_err_t time_task_start(void)
{
    if (task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(time_task, "time_task", PERIODIC_TASK_STACK_BYTES, NULL,
                    PERIODIC_TASK_PRIORITY, &task_handle) != pdPASS) {
        task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}