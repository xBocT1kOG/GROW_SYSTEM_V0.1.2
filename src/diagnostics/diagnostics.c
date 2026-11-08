#include "diagnostics.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------------------------------------------------------
// CUMULATIVE RAM COUNTERS
// ---------------------------------------------------------
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static diagnostics_snapshot_t totals;
static int64_t stack_sample_at[DIAG_TASK_COUNT];

void diagnostics_increment(diagnostics_counter_t counter)
{
    if ((unsigned)counter >= DIAG_COUNTER_COUNT) return;
    portENTER_CRITICAL(&lock);
    ++totals.counters[counter];
    portEXIT_CRITICAL(&lock);
}

void diagnostics_get_snapshot(diagnostics_snapshot_t *snapshot)
{
    portENTER_CRITICAL(&lock);
    *snapshot = totals;
    portEXIT_CRITICAL(&lock);
    snapshot->uptime_seconds = (uint64_t)esp_timer_get_time() / 1000000;
    snapshot->heap_free_bytes = esp_get_free_heap_size();
    snapshot->heap_min_free_bytes = esp_get_minimum_free_heap_size();
}

void diagnostics_sample_stack(diagnostics_task_t task)
{
    // Each task samples itself; deleted task handles are never retained.
    if ((unsigned)task >= DIAG_TASK_COUNT) return;
    int64_t now = esp_timer_get_time();
    if (stack_sample_at[task] && now - stack_sample_at[task] < 10000000) return;
    stack_sample_at[task] = now;
    uint32_t bytes = uxTaskGetStackHighWaterMark(NULL);
    portENTER_CRITICAL(&lock);
    totals.stack_bytes[task] = bytes;
    portEXIT_CRITICAL(&lock);
}

bool diagnostics_deadline_missed(int64_t elapsed_us, uint32_t period_ms)
{
    int64_t period_us = (int64_t)period_ms * 1000;
    int64_t margin = period_us / 10;
    if (margin < 250000) margin = 250000;
    return elapsed_us > period_us + margin;
}

void diagnostics_check_deadline(int64_t *previous_us, uint32_t period_ms)
{
    int64_t now = esp_timer_get_time();
    if (*previous_us >= 0 && diagnostics_deadline_missed(now - *previous_us, period_ms))
        diagnostics_increment(DIAG_task_deadline_misses);
    *previous_us = now;
}
