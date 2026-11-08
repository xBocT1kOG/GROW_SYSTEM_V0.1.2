#pragma once
#include <stdint.h>
#include "../config/app_config.h"

// ---------------------------------------------------------
// CALIBRATION CONVERSIONS
// ---------------------------------------------------------
static inline uint8_t sensor_adc_percent(int raw, int minimum, int maximum)
{
    if (raw <= minimum) return 0;
    if (raw >= maximum) return 100;
    return (uint8_t)(((int64_t)raw - minimum) * 100 / (maximum - minimum));
}

static inline uint8_t sensor_water_percent(float distance)
{
    if (distance >= WATER_DISTANCE_EMPTY_CM) return 0;
    if (distance <= WATER_DISTANCE_FULL_CM) return 100;
    return (uint8_t)((WATER_DISTANCE_EMPTY_CM - distance) * 100.0f /
                     (WATER_DISTANCE_EMPTY_CM - WATER_DISTANCE_FULL_CM));
}
