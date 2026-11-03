#pragma once

#include <stdio.h>
#include <string.h>
#include "../app/app_state.h"

// ---------------------------------------------------------
// LEGACY TWO-COLUMN TEXT CELLS
// ---------------------------------------------------------
#define DISPLAY_FIELD_COUNT 8
#define DISPLAY_CELL_CHARS 10

static inline const char *display_mode_text(light_mod_t mode)
{
    switch (mode) {
    case LIGHT_MOD_IDLE: return "IDLE";
    case LIGHT_MOD_DAWN: return "DAWN";
    case LIGHT_MOD_DUSK: return "DUSK";
    default: return "N/A";
    }
}

// Each cell occupies 60 pixels with the unchanged 5+1 pixel font.
static inline void display_format_snapshot(const app_state_t *state,
                                           char fields[DISPLAY_FIELD_COUNT][DISPLAY_CELL_CHARS + 1])
{
    const sensor_snapshot_t *s = &state->sensors;
    if (s->htu21_valid) {
        snprintf(fields[0], 11, "TEMP:%dC", (int)s->temperature_c);
        snprintf(fields[1], 11, "HUM:%u%%", (unsigned)s->humidity_percent);
    } else {
        snprintf(fields[0], 11, "TEMP:N/A");
        snprintf(fields[1], 11, "HUM:N/A");
    }
    if (s->soil_valid) snprintf(fields[2], 11, "SOIL:%u%%", (unsigned)s->soil_percent);
    else snprintf(fields[2], 11, "SOIL:N/A");
    if (s->water_valid) snprintf(fields[3], 11, "WATER:%u%%", (unsigned)s->water_percent);
    else snprintf(fields[3], 11, "WATER:N/A");

    // Full LIGHT_S/LIGHT_A labels do not fit; underscore is absent in this font.
    snprintf(fields[4], 11, "L-S:%u%%", (unsigned)state->light.system_level_percent);
    if (s->ldr_valid) snprintf(fields[5], 11, "L-A:%u%%", (unsigned)state->light.actual_percent);
    else snprintf(fields[5], 11, "L-A:N/A");
    snprintf(fields[6], 11, "AUTO:%s", state->light.auto_enabled ? "ON" : "OFF");
    snprintf(fields[7], 11, "MOD:%s", display_mode_text(state->light.mod));

    // Clear old trailing glyphs without clearing the whole screen every frame.
    for (unsigned i = 0; i < DISPLAY_FIELD_COUNT; ++i) {
        size_t length = strlen(fields[i]);
        memset(fields[i] + length, ' ', DISPLAY_CELL_CHARS - length);
        fields[i][DISPLAY_CELL_CHARS] = '\0';
    }
}
