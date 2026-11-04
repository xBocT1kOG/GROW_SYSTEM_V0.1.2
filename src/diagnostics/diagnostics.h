#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIAGNOSTIC_COUNTERS(X) \
    X(sensor_cycles) X(htu21_errors) X(soil_errors) X(ldr_errors) \
    X(ultrasonic_errors) X(ultrasonic_timeouts) \
    X(blynk_poll_ok) X(blynk_poll_errors) X(blynk_upload_ok) X(blynk_upload_errors) \
    X(wifi_reconnects) X(app_event_queue_overflows) X(button_events) \
    X(pump_commands) X(light_commands) X(auto_commands) \
    X(light_ramp_ticks) X(light_ramp_steps) X(time_sync_events) X(task_deadline_misses)
#define DIAGNOSTIC_TASKS(X) X(control) X(input) X(sensor) X(display) X(network) X(time) X(ramp)
#define DIAG_ENUM(name) DIAG_##name,
typedef enum { DIAGNOSTIC_COUNTERS(DIAG_ENUM) DIAG_COUNTER_COUNT } diagnostics_counter_t;
#undef DIAG_ENUM
#define DIAG_ENUM(name) DIAG_TASK_##name,
typedef enum { DIAGNOSTIC_TASKS(DIAG_ENUM) DIAG_TASK_COUNT } diagnostics_task_t;
#undef DIAG_ENUM

typedef struct {
    uint64_t uptime_seconds;
    uint64_t counters[DIAG_COUNTER_COUNT];
    size_t heap_free_bytes;
    size_t heap_min_free_bytes;
    uint32_t stack_bytes[DIAG_TASK_COUNT];
} diagnostics_snapshot_t;

void diagnostics_increment(diagnostics_counter_t counter);
void diagnostics_get_snapshot(diagnostics_snapshot_t *snapshot);
void diagnostics_sample_stack(diagnostics_task_t task);
bool diagnostics_deadline_missed(int64_t elapsed_us, uint32_t period_ms);
void diagnostics_check_deadline(int64_t *previous_us, uint32_t period_ms);
