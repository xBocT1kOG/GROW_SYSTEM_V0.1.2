#include "../diagnostics/diagnostics.h"
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
static app_state_t published_state;
static portMUX_TYPE state_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;

static void publish_state(const app_state_t *state)
{
    portENTER_CRITICAL(&state_snapshot_lock);
    published_state = *state;
    portEXIT_CRITICAL(&state_snapshot_lock);
}

// ---------------------------------------------------------
// LOCAL ACTUATOR COMMANDS
// ---------------------------------------------------------
static void set_pump(app_state_t *state, bool enabled)
{
    diagnostics_increment(DIAG_pump_commands);
    const bool previous = state->pump_enabled;
    if (previous == enabled) return;
    state->pump_enabled = enabled;
    const esp_err_t err = pump_set(state->pump_enabled);
    if (err != ESP_OK) {
        state->pump_enabled = previous; // Retain the last successful command.
        state->last_error = err;
        ESP_LOGE(TAG, "Pump update failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Pump: %s", state->pump_enabled ? "ON" : "OFF");
}

static void set_light(app_state_t *state, int next)
{
    diagnostics_increment(DIAG_light_commands);
    const uint8_t previous = state->light.system_level_percent;
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

static void set_auto(app_state_t *state, light_fsm_context_t *fsm, bool enabled)
{
    diagnostics_increment(DIAG_auto_commands);
    if (state->light.auto_enabled == enabled) return;
    const time_snapshot_t now = time_service_now();
    light_fsm_set_auto(&state->light, fsm, enabled, &now);
    ESP_LOGI(TAG, "Auto: %s", enabled ? "ON" : "OFF");
}

static void ramp_light(app_state_t *state)
{
    diagnostics_increment(DIAG_light_ramp_ticks);
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
    diagnostics_increment(DIAG_light_ramp_steps);
    ESP_LOGI(TAG, "Ramp: %u%%", (unsigned)state->light.system_level_percent);
}

// ---------------------------------------------------------
// EVENT HANDLING
// ---------------------------------------------------------

static void update_time_sync(app_state_t *state, const time_snapshot_t *now)
{
    if (!state->time_synced && now->synced) diagnostics_increment(DIAG_time_sync_events);
    if (!state->time_synced && now->synced && state->light.auto_enabled) {
        light_fsm_evaluate_current_phase(&state->light, now);
    }
    state->time_synced = now->synced;
}

// Only control_task calls this handler with its private state.
static void handle_event(app_state_t *state, light_fsm_context_t *fsm,
                         const app_event_t *event)
{
    const light_mod_t previous_mode = state->light.mod;
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
    case APP_EVT_TIME_SYNCED: {
        const time_snapshot_t now = time_service_now();
        update_time_sync(state, &now);
        break;
    }
    case APP_EVT_SYSTEM_ERROR:
        state->last_error = event->data.error;
        break;
    case APP_EVT_PUMP_TOGGLE:
        set_pump(state, !state->pump_enabled);
        break;
    case APP_EVT_LIGHT_INCREMENT:
        set_light(state, state->light.system_level_percent + LIGHT_STEP_PERCENT);
        break;
    case APP_EVT_LIGHT_DECREMENT:
        set_light(state, state->light.system_level_percent - LIGHT_STEP_PERCENT);
        break;
    case APP_EVT_LIGHT_AUTO_TOGGLE: {
        set_auto(state, fsm, !state->light.auto_enabled);
        break;
    }
    case APP_EVT_TIME_CHECK:
        if (state->time_synced != event->data.time.synced) {
            ESP_LOGI(TAG, "%s", event->data.time.synced
                     ? "Time synced: schedule ready" : "Time not synced: schedule skipped");
        }
        update_time_sync(state, &event->data.time);
        light_fsm_check_schedule(&state->light, fsm, &event->data.time);
        break;
    case APP_EVT_LIGHT_RAMP_TICK:
        ramp_light(state);
        break;
    case APP_EVT_BLYNK_PUMP_SET:
        set_pump(state, event->data.enabled);
        break;
    case APP_EVT_BLYNK_LIGHT_SET:
        set_light(state, event->data.light_percent);
        break;
    case APP_EVT_BLYNK_AUTO_SET:
        set_auto(state, fsm, event->data.enabled);
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
    diagnostics_sample_stack(DIAG_TASK_control);
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
    publish_state(&state);
    ESP_LOGI(TAG, "Local control ready: Pump OFF, Light 0%%, Auto OFF");
    ESP_LOGI(TAG, "Time not synced: schedule skipped until SNTP sync");

    for (;;) {
        diagnostics_sample_stack(DIAG_TASK_control);
        if (xQueueReceive(event_queue, &event, portMAX_DELAY) == pdTRUE) {
            handle_event(&state, &fsm, &event);
            publish_state(&state);
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
    if (xQueueSend(event_queue, event, wait_ticks) == pdTRUE) return ESP_OK;
    diagnostics_increment(DIAG_app_event_queue_overflows);
    return ESP_ERR_TIMEOUT;
}

esp_err_t app_control_get_sensors(sensor_snapshot_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&state_snapshot_lock);
    *snapshot = published_state.sensors;
    portEXIT_CRITICAL(&state_snapshot_lock);
    return ESP_OK;
}

esp_err_t app_state_get_snapshot(app_state_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&state_snapshot_lock);
    *snapshot = published_state;
    portEXIT_CRITICAL(&state_snapshot_lock);
    return ESP_OK;
}
