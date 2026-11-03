#pragma once

#include "app_state.h"
#include "../services/time_service.h"

// ---------------------------------------------------------
// CONTROL-OWNED FSM CONTEXT
// ---------------------------------------------------------
typedef struct {
    int last_dawn_date;
    int last_dusk_date;
    bool armed_time_valid;
    time_t armed_at;
} light_fsm_context_t;

// All functions are called only by control_task; no hardware access.
void light_fsm_set_auto(light_state_t *light, light_fsm_context_t *context,
                        bool enabled, const time_snapshot_t *now);
void light_fsm_check_schedule(light_state_t *light, light_fsm_context_t *context,
                              const time_snapshot_t *now);
void light_fsm_after_manual(light_state_t *light);
void light_fsm_ramp(light_state_t *light);