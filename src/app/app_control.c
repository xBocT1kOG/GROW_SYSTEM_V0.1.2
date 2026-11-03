#include "app_control.h"
#include "../config/app_config.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ---------------------------------------------------------
// PRIVATE CONTROL RESOURCES
// ---------------------------------------------------------
static QueueHandle_t event_queue;

// Only control_task calls this handler with its private state.
static void handle_event(app_state_t *state, const app_event_t *event)
{
    switch (event->type) {
    case APP_EVT_SENSOR_UPDATE:
        state->sensors = event->data.sensors;
        state->light.actual_percent = event->data.sensors.light_actual_percent;
        break;
    case APP_EVT_WIFI_CONNECTED:
        state->wifi_connected = true;
        break;
    case APP_EVT_WIFI_DISCONNECTED:
        state->wifi_connected = false;
        break;
    case APP_EVT_TIME_SYNCED:
        state->time_synced = true;
        break;
    case APP_EVT_SYSTEM_ERROR:
        state->last_error = event->data.error;
        break;
    case APP_EVT_PUMP_TOGGLE:
    case APP_EVT_LIGHT_INCREMENT:
    case APP_EVT_LIGHT_DECREMENT:
    case APP_EVT_LIGHT_AUTO_TOGGLE:
    case APP_EVT_BLYNK_PUMP_SET:
    case APP_EVT_BLYNK_LIGHT_SET:
    case APP_EVT_BLYNK_AUTO_SET:
    case APP_EVT_TIME_CHECK:
    case APP_EVT_LIGHT_RAMP_TICK:
        // Reserved for FSM migration; no actuator writes in this stage.
        break;
    default:
        break;
    }
}

static void control_task(void *argument)
{
    (void)argument;
    app_state_t state = {
        .light = {
            .auto_target_percent = LIGHT_AUTO_TARGET_PERCENT,
            .mod = LIGHT_MOD_IDLE,
        },
    };
    app_event_t event;

    for (;;) {
        if (xQueueReceive(event_queue, &event, portMAX_DELAY) == pdTRUE) {
            handle_event(&state, &event);
        }
    }
}

// ---------------------------------------------------------
// STARTUP AND EVENT SUBMISSION
// ---------------------------------------------------------
esp_err_t app_control_start(void)
{
    if (event_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    event_queue = xQueueCreate(APP_EVENT_QUEUE_LENGTH, sizeof(app_event_t));
    if (event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(control_task, "control_task", CONTROL_TASK_STACK_BYTES,
                    NULL, CONTROL_TASK_PRIORITY, NULL) != pdPASS) {
        vQueueDelete(event_queue);
        event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t app_control_post_event(const app_event_t *event, TickType_t wait_ticks)
{
    if (event == NULL || event->type < 0 || event->type >= APP_EVT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((event->type == APP_EVT_BLYNK_LIGHT_SET && event->data.light_percent > 100) ||
        (event->type == APP_EVT_SENSOR_UPDATE &&
         (event->data.sensors.soil_percent > 100 ||
          event->data.sensors.water_percent > 100 ||
          event->data.sensors.light_actual_percent > 100))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(event_queue, event, wait_ticks) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
}
