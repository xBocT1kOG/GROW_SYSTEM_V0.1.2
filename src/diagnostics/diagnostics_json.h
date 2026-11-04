#pragma once
#include "diagnostics.h"
#include "../app/app_state.h"

bool diagnostics_json(char *buffer, size_t capacity, const diagnostics_snapshot_t *snapshot);
bool diagnostics_state_json(char *buffer, size_t capacity, const app_state_t *state);
