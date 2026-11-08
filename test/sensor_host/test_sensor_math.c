#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../../src/drivers/sensor_math.h"
#include "../../src/tasks/sensor_filter.h"

int main(void)
{
    // Both ADC channels use the same increasing calibration endpoints.
    assert(sensor_adc_percent(-1, 500, 3800) == 0);
    assert(sensor_adc_percent(500, 500, 3800) == 0);
    assert(sensor_adc_percent(2150, 500, 3800) == 50);
    assert(sensor_adc_percent(3800, 500, 3800) == 100);
    assert(sensor_adc_percent(4095, 500, 3800) == 100);
    for (int raw = 0; raw < 4095; ++raw) {
        assert(sensor_adc_percent(raw, 500, 3800) <=
               sensor_adc_percent(raw + 1, 500, 3800));
    }
    assert(sensor_water_percent(16.0f) == 0);
    assert(sensor_water_percent(7.0f) == 100);
    assert(sensor_water_percent(11.5f) == 50);
    assert(sensor_water_percent(25.0f) == 0);
    assert(sensor_water_percent(3.0f) == 100);

    water_filter_t filter = {0};
    float median = -1;
    assert(!water_filter_add(&filter, NAN, &median));
    assert(filter.count == 0 && median == -1);
    assert(!water_filter_add(&filter, 0, &median));
    assert(!water_filter_add(&filter, -2, &median));
    assert(!water_filter_add(&filter, 401, &median));
    assert(!water_filter_add(&filter, INFINITY, &median));

    assert(water_filter_add(&filter, 10, &median) && median == 10);
    assert(water_filter_add(&filter, 14, &median) && median == 12);
    assert(water_filter_add(&filter, 12, &median) && median == 12);
    assert(water_filter_add(&filter, 400, &median) && median == 13);
    assert(water_filter_add(&filter, 8, &median) && median == 12);
    assert(filter.count == 5);
    unsigned next = filter.next;
    assert(!water_filter_add(&filter, 401, &median));
    assert(filter.next == next && filter.count == 5 && median == 12);
    // Ring wrap replaces the oldest valid sample, not an error placeholder.
    assert(water_filter_add(&filter, 16, &median) && median == 14);
    assert(water_filter_add(&filter, 7, &median) && median == 12);
    puts("Sensor calibration and rolling median tests passed");
    return 0;
}
