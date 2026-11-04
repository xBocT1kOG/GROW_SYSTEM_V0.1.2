#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "../../src/diagnostics/diagnostics_json.h"
#include "../../src/app/light_fsm.h"
#include "cJSON.h"

static int64_t clock_us = 1000000;
static uint32_t stack_bytes = 2048;
int64_t esp_timer_get_time(void) { return clock_us; }
size_t esp_get_free_heap_size(void) { return 100000; }
size_t esp_get_minimum_free_heap_size(void) { return 90000; }
uint32_t uxTaskGetStackHighWaterMark(void *task) { assert(task == NULL); return stack_bytes; }

static DWORD WINAPI increment_worker(void *arg)
{
    (void)arg;
    for (unsigned i = 0; i < 100000; ++i) diagnostics_increment(DIAG_sensor_cycles);
    return 0;
}

int main(void)
{
    diagnostics_snapshot_t s, saved;
    diagnostics_get_snapshot(&s);
    assert(s.uptime_seconds == 1 && s.heap_free_bytes == 100000 && s.heap_min_free_bytes == 90000);
    for (unsigned i = 0; i < DIAG_COUNTER_COUNT; ++i) assert(s.counters[i] == 0);
    HANDLE workers[4];
    for (unsigned i = 0; i < 4; ++i) {
        workers[i] = CreateThread(NULL, 0, increment_worker, NULL, 0, NULL);
        assert(workers[i]);
    }
    uint64_t previous = 0;
    while (WaitForMultipleObjects(4, workers, TRUE, 0) == WAIT_TIMEOUT) {
        diagnostics_get_snapshot(&s);
        assert(s.counters[DIAG_sensor_cycles] >= previous);
        assert(s.counters[DIAG_sensor_cycles] <= 400000);
        previous = s.counters[DIAG_sensor_cycles];
    }
    for (unsigned i = 0; i < 4; ++i) CloseHandle(workers[i]);
    diagnostics_get_snapshot(&saved);
    assert(saved.counters[DIAG_sensor_cycles] == 400000);
    diagnostics_increment(DIAG_sensor_cycles);
    diagnostics_increment(DIAG_app_event_queue_overflows);
    diagnostics_get_snapshot(&s);
    assert(s.counters[DIAG_sensor_cycles] == 400001 && saved.counters[DIAG_sensor_cycles] == 400000);
    assert(s.counters[DIAG_app_event_queue_overflows] == 1);

    // IDLE ticks count, but only brightness changes count as steps.
    light_state_t light = { .auto_enabled = true, .mod = LIGHT_MOD_IDLE, .system_level_percent = 15 };
    for (unsigned i = 0; i < 2; ++i) {
        unsigned before = light.system_level_percent;
        diagnostics_increment(DIAG_light_ramp_ticks);
        light_fsm_ramp(&light);
        if (before != light.system_level_percent) diagnostics_increment(DIAG_light_ramp_steps);
        light.mod = LIGHT_MOD_DUSK;
    }
    diagnostics_get_snapshot(&s);
    assert(s.counters[DIAG_light_ramp_ticks] == 2 && s.counters[DIAG_light_ramp_steps] == 1);
    assert(!diagnostics_deadline_missed(750000, 500));
    assert(diagnostics_deadline_missed(750001, 500));
    assert(!diagnostics_deadline_missed(660000000, 600000));
    assert(diagnostics_deadline_missed(660000001, 600000));
    int64_t previous_us = -1;
    diagnostics_check_deadline(&previous_us, 500);
    clock_us += 750001;
    diagnostics_check_deadline(&previous_us, 500);
    diagnostics_get_snapshot(&s);
    assert(s.counters[DIAG_task_deadline_misses] == 1);
    diagnostics_sample_stack(DIAG_TASK_control);
    stack_bytes = 1024;
    diagnostics_sample_stack(DIAG_TASK_control);
    diagnostics_get_snapshot(&s);
    assert(s.stack_bytes[DIAG_TASK_control] == 2048);
    clock_us += 10000000;
    diagnostics_sample_stack(DIAG_TASK_control);
    diagnostics_get_snapshot(&s);
    assert(s.stack_bytes[DIAG_TASK_control] == 1024);

    char json[2048];
    for (unsigned i = 0; i < DIAG_COUNTER_COUNT; ++i) s.counters[i] = UINT64_MAX;
    assert(diagnostics_json(json, sizeof(json), &s));
    assert(strstr(json, "18446744073709551615"));
    cJSON *root = cJSON_Parse(json);
    assert(root && cJSON_GetObjectItem(root, "stack_control_bytes"));
    assert(cJSON_GetArraySize(root) == 3 + DIAG_COUNTER_COUNT + DIAG_TASK_COUNT);
    cJSON_Delete(root);
    assert(!diagnostics_json(json, 8, &s));
    app_state_t state = { .pump_enabled = true, .light = { .system_level_percent = 55,
        .actual_percent = 19, .auto_enabled = true, .mod = LIGHT_MOD_DUSK } };
    state.sensors.temperature_c = NAN;
    assert(diagnostics_state_json(json, sizeof(json), &state));
    root = cJSON_Parse(json);
    assert(root && cJSON_IsNull(cJSON_GetObjectItem(root, "temperature")));
    assert(cJSON_GetObjectItem(root, "LIGHT_S")->valueint == 55);
    assert(cJSON_GetObjectItem(root, "LIGHT_A")->valueint == 19);
    assert(cJSON_GetObjectItem(root, "MOD")->valueint == 2);
    cJSON_Delete(root);
    state.sensors.htu21_valid = true;
    state.sensors.temperature_c = 23.25f;
    state.sensors.humidity_percent = 44.5f;
    assert(diagnostics_state_json(json, sizeof(json), &state));
    root = cJSON_Parse(json);
    assert(root && cJSON_GetObjectItem(root, "temperature")->valuedouble == 23.25);
    cJSON_Delete(root);
    assert(!diagnostics_state_json(json, 8, &state));
    puts("Diagnostics counters, concurrency, snapshots, deadlines and JSON tests passed");
    return 0;
}
