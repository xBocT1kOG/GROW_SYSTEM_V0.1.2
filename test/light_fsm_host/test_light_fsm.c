#include <assert.h>
#include <stdio.h>
#include "../../src/app/light_fsm.h"

static time_snapshot_t at(int day, int hour, int minute, int second)
{
    return (time_snapshot_t){
        .synced = true, .year = 2026, .day_of_year = day,
        .hour = hour, .minute = minute, .second = second,
        .timestamp = (time_t)day * 86400 + hour * 3600 + minute * 60 + second,
    };
}

static light_state_t light_at(unsigned level)
{
    return (light_state_t){ .system_level_percent = (uint8_t)level,
        .auto_target_percent = 60, .auto_enabled = true, .mod = LIGHT_MOD_IDLE };
}

int main(void)
{
    light_fsm_context_t ctx = {0};
    light_state_t light = light_at(20);
    time_snapshot_t now = at(1, 6, 0, 10);

    // Invalid time and AUTO OFF cannot start a cycle.
    now.synced = false;
    light_fsm_check_schedule(&light, &ctx, &now);
    assert(light.mod == LIGHT_MOD_IDLE);
    now.synced = true;
    light.auto_enabled = false;
    light_fsm_check_schedule(&light, &ctx, &now);
    light_fsm_set_auto(&light, &ctx, true, &now);
    light_fsm_check_schedule(&light, &ctx, &now);
    assert(light.mod == LIGHT_MOD_IDLE && light.system_level_percent == 20);

    // DAWN starts without an immediate step and stops at 60.
    now = at(2, 6, 0, 10);
    light_fsm_check_schedule(&light, &ctx, &now);
    assert(light.mod == LIGHT_MOD_DAWN && light.system_level_percent == 20);
    for (unsigned expected = 25; expected <= 60; expected += 5) {
        light_fsm_ramp(&light);
        assert(light.system_level_percent == expected);
    }
    assert(light.mod == LIGHT_MOD_IDLE);
    light.system_level_percent = 20;
    light_fsm_check_schedule(&light, &ctx, &now);
    assert(light.mod == LIGHT_MOD_IDLE); // Same minute cannot restart it.

    // A backward date correction does not replay a consumed cycle.
    now = at(1, 6, 0, 30);
    light_fsm_check_schedule(&light, &ctx, &now);
    assert(light.mod == LIGHT_MOD_IDLE);

    // DUSK is independent of today's DAWN key; stops exactly at zero.
    light.system_level_percent = 50;
    now = at(2, 20, 0, 10);
    light_fsm_check_schedule(&light, &ctx, &now);
    assert(light.mod == LIGHT_MOD_DUSK && light.system_level_percent == 50);
    for (int expected = 45; expected >= 0; expected -= 5) {
        light_fsm_ramp(&light);
        assert(light.system_level_percent == expected);
    }
    assert(light.mod == LIGHT_MOD_IDLE);

    // Manual changes preserve active direction until its endpoint.
    light = light_at(40);
    light.mod = LIGHT_MOD_DUSK;
    light_fsm_after_manual(&light);
    assert(light.mod == LIGHT_MOD_DUSK);
    light_fsm_ramp(&light);
    assert(light.system_level_percent == 35);
    light.mod = LIGHT_MOD_DAWN;
    light_fsm_after_manual(&light);
    assert(light.mod == LIGHT_MOD_DAWN);
    light_fsm_ramp(&light);
    assert(light.system_level_percent == 40);
    light.system_level_percent = 80;
    light_fsm_after_manual(&light);
    assert(light.mod == LIGHT_MOD_IDLE && light.system_level_percent == 80);

    // DAWN never reduces an above-target value, including at schedule time.
    now = at(3, 6, 0, 10);
    light_fsm_check_schedule(&light, &ctx, &now);
    assert(light.mod == LIGHT_MOD_IDLE && light.system_level_percent == 80);
    light.mod = LIGHT_MOD_DAWN;
    light_fsm_ramp(&light);
    assert(light.mod == LIGHT_MOD_IDLE && light.system_level_percent == 80);

    // AUTO OFF cancels immediately without changing brightness.
    light.mod = LIGHT_MOD_DUSK;
    light_fsm_set_auto(&light, &ctx, false, &now);
    light_fsm_ramp(&light);
    assert(light.mod == LIGHT_MOD_IDLE && light.system_level_percent == 80);

    // AUTO ON at 21:00 never catches up.
    now = at(3, 21, 0, 0);
    light_fsm_set_auto(&light, &ctx, true, &now);
    light_fsm_check_schedule(&light, &ctx, &now);
    assert(light.mod == LIGHT_MOD_IDLE && light.system_level_percent == 80);

    // AUTO ON during the scheduled minute is also not a future trigger.
    ctx = (light_fsm_context_t){0};
    light = light_at(20);
    now = at(4, 6, 0, 5);
    light_fsm_set_auto(&light, &ctx, true, &now);
    now = at(4, 6, 0, 50);
    light_fsm_check_schedule(&light, &ctx, &now);
    assert(light.mod == LIGHT_MOD_IDLE);
    now = at(5, 6, 0, 10);
    light_fsm_check_schedule(&light, &ctx, &now);
    assert(light.mod == LIGHT_MOD_DAWN);

    // Non-multiple endpoints clamp; LDR values never affect ramp output.
    light.system_level_percent = 58;
    light.actual_percent = 100;
    light_fsm_ramp(&light);
    assert(light.system_level_percent == 60 && light.mod == LIGHT_MOD_IDLE);
    light.system_level_percent = 3;
    light.mod = LIGHT_MOD_DUSK;
    light_fsm_ramp(&light);
    assert(light.system_level_percent == 0 && light.mod == LIGHT_MOD_IDLE);

    puts("Light FSM behavior tests passed");
    return 0;
}
