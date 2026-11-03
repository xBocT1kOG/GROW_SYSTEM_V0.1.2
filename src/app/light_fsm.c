#include "light_fsm.h"
#include "../config/app_config.h"

// ---------------------------------------------------------
// AUTO AND SCHEDULE HANDLING
// ---------------------------------------------------------
void light_fsm_set_auto(light_state_t *light, light_fsm_context_t *context,
                        bool enabled, const time_snapshot_t *now)
{
    light->auto_enabled = enabled;
    if (!enabled) {
        light->mod = LIGHT_MOD_IDLE;
        return;
    }
    context->armed_time_valid = now->synced;
    context->armed_at = now->timestamp;
}

void light_fsm_check_schedule(light_state_t *light, light_fsm_context_t *context,
                              const time_snapshot_t *now)
{
    if (!now->synced || now->minute != 0) {
        return;
    }
    int *last_date;
    if (now->hour == LIGHT_DAWN_START_HOUR) {
        last_date = &context->last_dawn_date;
    } else if (now->hour == LIGHT_DUSK_START_HOUR) {
        last_date = &context->last_dusk_date;
    } else {
        return;
    }

    const int date = now->year * 1000 + now->day_of_year;
    if (date <= *last_date) {
        return; // Also reject replay after a backward clock correction.
    }
    *last_date = date; // Consume scheduled minutes even while AUTO is off.
    if (!light->auto_enabled ||
        (context->armed_time_valid && now->timestamp - now->second <= context->armed_at)) {
        return; // Enabling AUTO in the scheduled minute does not catch up.
    }
    if (now->hour == LIGHT_DAWN_START_HOUR) {
        light->mod = light->system_level_percent < light->auto_target_percent
            ? LIGHT_MOD_DAWN : LIGHT_MOD_IDLE;
    } else {
        light->mod = light->system_level_percent > 0 ? LIGHT_MOD_DUSK : LIGHT_MOD_IDLE;
    }
}

// ---------------------------------------------------------
// MANUAL ENDPOINTS AND PERIODIC RAMP
// ---------------------------------------------------------
void light_fsm_after_manual(light_state_t *light)
{
    if ((light->mod == LIGHT_MOD_DAWN &&
         light->system_level_percent >= light->auto_target_percent) ||
        (light->mod == LIGHT_MOD_DUSK && light->system_level_percent == 0)) {
        light->mod = LIGHT_MOD_IDLE;
    }
}

void light_fsm_ramp(light_state_t *light)
{
    if (!light->auto_enabled) {
        light->mod = LIGHT_MOD_IDLE;
        return;
    }
    switch (light->mod) {
    case LIGHT_MOD_DAWN: {
        if (light->system_level_percent >= light->auto_target_percent) {
            light->mod = LIGHT_MOD_IDLE; // Never lower a manually raised level.
            break;
        }
        unsigned next = light->system_level_percent + LIGHT_STEP_PERCENT;
        if (next >= light->auto_target_percent) {
            next = light->auto_target_percent;
            light->mod = LIGHT_MOD_IDLE;
        }
        light->system_level_percent = (uint8_t)next;
        break;
    }
    case LIGHT_MOD_DUSK:
        if (light->system_level_percent <= LIGHT_STEP_PERCENT) {
            light->system_level_percent = 0;
            light->mod = LIGHT_MOD_IDLE;
        } else {
            light->system_level_percent -= LIGHT_STEP_PERCENT;
        }
        break;
    case LIGHT_MOD_IDLE:
    default:
        break;
    }
}