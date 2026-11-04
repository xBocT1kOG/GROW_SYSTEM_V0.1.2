#include "diagnostics_json.h"
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

static bool append(char **buffer, size_t *capacity, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int count = vsnprintf(*buffer, *capacity, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= *capacity) return false;
    *buffer += count;
    *capacity -= (size_t)count;
    return true;
}

bool diagnostics_json(char *buffer, size_t capacity, const diagnostics_snapshot_t *s)
{
    if (!append(&buffer, &capacity, "{\"uptime_seconds\":%" PRIu64, s->uptime_seconds)) return false;
#define FIELD(name) if (!append(&buffer, &capacity, ",\"" #name "\":%" PRIu64, s->counters[DIAG_##name])) return false;
    DIAGNOSTIC_COUNTERS(FIELD)
#undef FIELD
    if (!append(&buffer, &capacity, ",\"heap_free_bytes\":%zu,\"heap_min_free_bytes\":%zu",
                s->heap_free_bytes, s->heap_min_free_bytes)) return false;
#define FIELD(name) if (!append(&buffer, &capacity, ",\"stack_" #name "_bytes\":%" PRIu32, s->stack_bytes[DIAG_TASK_##name])) return false;
    DIAGNOSTIC_TASKS(FIELD)
#undef FIELD
    return append(&buffer, &capacity, "}");
}

bool diagnostics_state_json(char *buffer, size_t capacity, const app_state_t *s)
{
    if (!append(&buffer, &capacity,
        "{\"pump\":%u,\"LIGHT_S\":%u,\"LIGHT_A\":%u,\"AUTO\":%u,\"MOD\":%u,"
        "\"soil_percent\":%u,\"water_percent\":%u,\"wifi_connected\":%s,\"time_synced\":%s",
        s->pump_enabled ? 1U : 0U, (unsigned)s->light.system_level_percent,
        (unsigned)s->light.actual_percent, s->light.auto_enabled ? 1U : 0U,
        (unsigned)s->light.mod, (unsigned)s->sensors.soil_percent, (unsigned)s->sensors.water_percent,
        s->wifi_connected ? "true" : "false", s->time_synced ? "true" : "false")) return false;
    if (s->sensors.htu21_valid && isfinite(s->sensors.temperature_c) && isfinite(s->sensors.humidity_percent)) {
        if (!append(&buffer, &capacity, ",\"temperature\":%.2f,\"humidity\":%.2f",
                    (double)s->sensors.temperature_c, (double)s->sensors.humidity_percent)) return false;
    } else if (!append(&buffer, &capacity, ",\"temperature\":null,\"humidity\":null")) return false;
    return append(&buffer, &capacity,
        ",\"htu21_valid\":%s,\"soil_valid\":%s,\"ldr_valid\":%s,\"water_valid\":%s,"
        "\"htu21_error\":%d,\"soil_error\":%d,\"ldr_error\":%d,\"water_error\":%d}",
        s->sensors.htu21_valid ? "true" : "false", s->sensors.soil_valid ? "true" : "false",
        s->sensors.ldr_valid ? "true" : "false", s->sensors.water_valid ? "true" : "false",
        s->sensors.htu21_error, s->sensors.soil_error, s->sensors.ldr_error, s->sensors.water_error);
}
