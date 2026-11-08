#pragma once
#include <stdbool.h>
#include <math.h>

// ---------------------------------------------------------
// FIVE VALID SAMPLES, OWNED BY SENSOR TASK
// ---------------------------------------------------------
typedef struct {
    float values[5];
    unsigned count;
    unsigned next;
} water_filter_t;

// Invalid samples leave the history and output unchanged.
static inline bool water_filter_add(water_filter_t *filter, float distance, float *median)
{
    if (!isfinite(distance) || distance <= 0 || distance > 400) return false;
    filter->values[filter->next] = distance;
    filter->next = (filter->next + 1) % 5;
    if (filter->count < 5) ++filter->count;
    float sorted[5];
    for (unsigned i = 0; i < filter->count; ++i) {
        sorted[i] = filter->values[i];
        for (unsigned j = i; j > 0 && sorted[j] < sorted[j - 1]; --j) {
            float temp = sorted[j];
            sorted[j] = sorted[j - 1];
            sorted[j - 1] = temp;
        }
    }
    unsigned middle = filter->count / 2;
    *median = filter->count % 2 ? sorted[middle] :
        (sorted[middle - 1] + sorted[middle]) / 2.0f;
    return true;
}
