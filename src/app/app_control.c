#include "app_control.h"
#include "light_fsm.h"
#include "../config/app_config.h"
#include "../drivers/pump.h"
#include "../drivers/light_pwm.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ---------------------------------------------------------
// PRIVATE CONTROL RESOURCES
// ---------------------------------------------------------
static QueueHandle_t event_queue;
static const char *TAG = "app_control";
static sensor_snapshot_t published_sensors;
static portMUX_TYPE sensor_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------
// LOCAL ACTUATOR COMMANDS
// ---------------------------------------------------------
static void toggle_pump(app_state_t *state)
{
    const bool previous = state->pump_enabled;
    state->pump_enabled = !previous;
    const esp_err_t err = pump_set(state->pump_enabled);
    if (err != ESP_OK) {
        state->pump_enabled = previous; // Retain the last successful command.
        state->last_error = err;
        ESP_LOGE(TAG, "Pump update failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Pump: %s", state->pump_enabled ? "ON" : "OFF");
}

static void change_light(app_state_t *state, bool increase)
{
    const uint8_t previous = state->light.system_level_percent;
    int next = (int)previous + (increase ? LIGHT_STEP_PERCENT : -LIGHT_STEP_PERCENT);
    if (next > 100) {
        next = 100;
    } else if (next < 0) {
        next = 0;
    }
    if (next == previous) {
        return; // No repeated hardware writes or logs at the limits.
    }
    state->light.system_level_percent = (uint8_t)next;
    const esp_err_t err = light_pwm_set_percent(state->light.system_level_percent);
    if (err != ESP_OK) {
        state->light.system_level_percent = previous;
        state->last_error = err;
        ESP_LOGE(TAG, "Light update failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Light: %u%%", (unsigned)state->light.system_level_percent);
    light_fsm_after_manual(&state->light);
}

static void ramp_light(app_state_t *state)
{
    const light_state_t previous = state->light;
    light_fsm_ramp(&state->light);
    if (state->light.system_level_percent == previous.system_level_percent) {
        return;
    }
    const esp_err_t err = light_pwm_set_percent(state->light.system_level_percent);
    if (err != ESP_OK) {
        state->light = previous; // Retry the same step at the next ramp tick.
        state->last_error = err;
        ESP_LOGE(TAG, "Ramp failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Ramp: %u%%", (unsigned)state->light.system_level_percent);
}

// ---------------------------------------------------------
// EVENT HANDLING
// ---------------------------------------------------------

// Only control_task calls this handler with its private state.
static void handle_event(app_state_t *state, light_fsm_context_t *fsm,
                         const app_event_t *event)
{
    const light_mod_t previous_mode = state->light.mod;
    switch (event->type) {
    case APP_EVT_SENSOR_UPDATE:
        state->sensors = event->data.sensors;
        state->light.actual_percent = event->data.sensors.light_actual_percent;
        portENTER_CRITICAL(&sensor_snapshot_lock);
        published_sensors = state->sensors;
        portEXIT_CRITICAL(&sensor_snapshot_lock);
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
        toggle_pump(state);
        break;
    case APP_EVT_LIGHT_INCREMENT:
        change_light(state, true);
        break;
    case APP_EVT_LIGHT_DECREMENT:
        change_light(state, false);
        break;
    case APP_EVT_LIGHT_AUTO_TOGGLE: {
        const time_snapshot_t now = time_service_now();
        light_fsm_set_auto(&state->light, fsm, !state->light.auto_enabled, &now);
        ESP_LOGI(TAG, "Auto: %s", state->light.auto_enabled ? "ON" : "OFF");
        break;
    }
    case APP_EVT_TIME_CHECK:
        if (state->time_synced != event->data.time.synced) {
            ESP_LOGI(TAG, "%s", event->data.time.synced
                     ? "Time synced: schedule ready" : "Time not synced: schedule skipped");
        }
        state->time_synced = event->data.time.synced;
        light_fsm_check_schedule(&state->light, fsm, &event->data.time);
        break;
    case APP_EVT_LIGHT_RAMP_TICK:
        ramp_light(state);
        break;
    case APP_EVT_BLYNK_PUMP_SET:
    case APP_EVT_BLYNK_LIGHT_SET:
    case APP_EVT_BLYNK_AUTO_SET:
        // Network control remains inactive.
        break;
    default:
        break;
    }
    if (state->light.mod != previous_mode) {
        const char *mode = state->light.mod == LIGHT_MOD_DAWN ? "DAWN" :
                           state->light.mod == LIGHT_MOD_DUSK ? "DUSK" : "IDLE";
        ESP_LOGI(TAG, "Light mode: %s", mode);
    }
}

static void control_task(void *argument)
{
    (void)argument;
    app_state_t state = {
        .light = {
            .system_level_percent = 0,
            .auto_target_percent = LIGHT_AUTO_TARGET_PERCENT,
            .actual_percent = 0,
            .auto_enabled = false,
            .mod = LIGHT_MOD_IDLE,
        },
        .pump_enabled = false,
    };
    app_event_t event;
    light_fsm_context_t fsm = {0};
    ESP_LOGI(TAG, "Local control ready: Pump OFF, Light 0%%, Auto OFF");
    ESP_LOGI(TAG, "Time not synced: schedule skipped until SNTP sync");

    for (;;) {
        if (xQueueReceive(event_queue, &event, portMAX_DELAY) == pdTRUE) {
            handle_event(&state, &fsm, &event);
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

esp_err_t app_control_get_sensors(sensor_snapshot_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&sensor_snapshot_lock);
    *snapshot = published_sensors;
    portEXIT_CRITICAL(&sensor_snapshot_lock);
    return ESP_OK;
}
