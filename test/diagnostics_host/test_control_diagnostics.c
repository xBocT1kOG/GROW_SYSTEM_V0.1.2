#include <assert.h>
#include <stdio.h>
#include "../../src/app/app_control.c"

static bool queue_full;
static esp_err_t pwm_result;
static time_snapshot_t test_time;
int64_t esp_timer_get_time(void) { return 1000000; }
size_t esp_get_free_heap_size(void) { return 100000; }
size_t esp_get_minimum_free_heap_size(void) { return 90000; }
uint32_t uxTaskGetStackHighWaterMark(void *task) { (void)task; return 2048; }
QueueHandle_t xQueueCreate(unsigned count, unsigned size) { (void)count; (void)size; return (void *)1; }
void vQueueDelete(QueueHandle_t queue) { (void)queue; }
int xQueueSend(QueueHandle_t queue, const void *event, TickType_t wait)
{ (void)queue; (void)event; (void)wait; return !queue_full; }
int xQueueReceive(QueueHandle_t queue, void *event, TickType_t wait)
{ (void)queue; (void)event; (void)wait; return 0; }
int xTaskCreate(void (*function)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, TaskHandle_t *handle)
{ (void)function; (void)name; (void)stack; (void)arg; (void)priority; (void)handle; return pdPASS; }
esp_err_t pump_set(bool enabled) { (void)enabled; return ESP_OK; }
esp_err_t light_pwm_set_percent(uint8_t value) { (void)value; return pwm_result; }
time_snapshot_t time_service_now(void) { return test_time; }

int main(void)
{
    diagnostics_snapshot_t d;
    app_event_t event = { .type = APP_EVT_LIGHT_RAMP_TICK };
    assert(app_control_post_event(&event, 0) == ESP_ERR_INVALID_STATE);
    assert(app_control_start() == ESP_OK);
    assert(app_control_post_event(&event, 0) == ESP_OK);
    queue_full = true;
    assert(app_control_post_event(&event, 0) == ESP_ERR_TIMEOUT);
    assert(app_control_post_event(NULL, 0) == ESP_ERR_INVALID_ARG);
    diagnostics_get_snapshot(&d);
    assert(d.counters[DIAG_app_event_queue_overflows] == 1);
    app_state_t state = { .light = { .auto_enabled = true, .system_level_percent = 15,
                                    .auto_target_percent = 60, .mod = LIGHT_MOD_IDLE } };
    light_fsm_context_t fsm = {0};
    handle_event(&state, &fsm, &event);
    assert(state.light.system_level_percent == 15);
    state.light.mod = LIGHT_MOD_DUSK;
    handle_event(&state, &fsm, &event);
    assert(state.light.system_level_percent == 10);
    pwm_result = ESP_FAIL;
    handle_event(&state, &fsm, &event);
    assert(state.light.system_level_percent == 10);
    diagnostics_get_snapshot(&d);
    assert(d.counters[DIAG_light_ramp_ticks] == 3 && d.counters[DIAG_light_ramp_steps] == 1);
    pwm_result = ESP_OK;
    event.type = APP_EVT_PUMP_TOGGLE;
    handle_event(&state, &fsm, &event);
    event.type = APP_EVT_BLYNK_PUMP_SET;
    event.data.enabled = true;
    handle_event(&state, &fsm, &event);
    event.type = APP_EVT_LIGHT_INCREMENT;
    handle_event(&state, &fsm, &event);
    event.type = APP_EVT_BLYNK_LIGHT_SET;
    event.data.light_percent = 55;
    handle_event(&state, &fsm, &event);
    event.type = APP_EVT_LIGHT_AUTO_TOGGLE;
    handle_event(&state, &fsm, &event);
    event.type = APP_EVT_BLYNK_AUTO_SET;
    event.data.enabled = true;
    handle_event(&state, &fsm, &event);
    diagnostics_get_snapshot(&d);
    assert(d.counters[DIAG_pump_commands] == 2 && d.counters[DIAG_light_commands] == 2);
    assert(d.counters[DIAG_auto_commands] == 2);
    test_time.synced = true;
    test_time.hour = 3;
    event.type = APP_EVT_TIME_SYNCED;
    handle_event(&state, &fsm, &event);
    handle_event(&state, &fsm, &event);
    diagnostics_get_snapshot(&d);
    assert(d.counters[DIAG_time_sync_events] == 1 && state.light.mod == LIGHT_MOD_DUSK);
    publish_state(&state);
    app_state_t copy;
    assert(app_state_get_snapshot(&copy) == ESP_OK);
    assert(copy.light.system_level_percent == 55 && copy.pump_enabled);
    puts("Production control instrumentation and queue failure tests passed");
    return 0;
}
