#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/tasks/display_format.h"
#include "../../src/SSD1306_CustomLib.h"

i2c_master_bus_handle_t bus_handle;
i2c_master_dev_handle_t oled_handle;
static uint8_t commands[128];
static unsigned command_count;
static unsigned pixel_columns;

int i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *data,
                        size_t length, int timeout)
{
    (void)device;
    (void)timeout;
    if (data[0] == 0) {
        assert(length == 2 && command_count < sizeof(commands));
        commands[command_count++] = data[1];
    } else {
        assert(data[0] == 0x40 && length >= 2 && length <= 129);
        pixel_columns += (unsigned)length - 1;
    }
    return 0;
}

int main(void)
{
    // Verify the actual custom library's original initialization sequence.
    const uint8_t expected[] = {
        0xAE, 0x20, 0x00, 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07,
        0x81, 0x7F, 0xA1, 0xC8, 0xA6, 0xA8, 0x3F, 0xD3, 0x00,
        0xD5, 0x80, 0xD9, 0x22, 0xDA, 0x12, 0xDB, 0x20, 0x8D, 0x14, 0xAF
    };
    oled_init();
    assert(command_count == sizeof(expected));
    assert(memcmp(commands, expected, sizeof(expected)) == 0);
    command_count = 0;
    oled_clear();
    assert(command_count == 24 && pixel_columns == 1024);

    app_state_t state = {0};
    char fields[8][11];
    display_format_snapshot(&state, fields);
    assert(strcmp(fields[0], "TEMP:N/A  ") == 0);
    assert(strcmp(fields[5], "L-A:N/A   ") == 0);
    assert(strcmp(fields[6], "AUTO:OFF  ") == 0);
    assert(strcmp(fields[7], "MOD:IDLE  ") == 0);

    state.sensors.htu21_valid = state.sensors.soil_valid = true;
    state.sensors.water_valid = state.sensors.ldr_valid = true;
    state.sensors.temperature_c = -40;
    state.sensors.humidity_percent = 100;
    state.sensors.soil_percent = state.sensors.water_percent = 100;
    state.light.system_level_percent = 100;
    state.light.actual_percent = 35;
    state.light.auto_enabled = true;
    state.light.mod = LIGHT_MOD_DUSK;
    display_format_snapshot(&state, fields);
    assert(strcmp(fields[0], "TEMP:-40C ") == 0);
    assert(strcmp(fields[1], "HUM:100%  ") == 0);
    assert(strcmp(fields[2], "SOIL:100% ") == 0);
    assert(strcmp(fields[3], "WATER:100%") == 0);
    assert(strcmp(fields[4], "L-S:100%  ") == 0);
    assert(strcmp(fields[5], "L-A:35%   ") == 0);
    assert(strcmp(fields[6], "AUTO:ON   ") == 0);
    assert(strcmp(fields[7], "MOD:DUSK  ") == 0);

    for (unsigned i = 0; i < 8; ++i) {
        assert(strlen(fields[i]) == 10);
        for (unsigned j = 0; j < 10; ++j) assert(fields[i][j] >= 32 && fields[i][j] <= 90);
        command_count = pixel_columns = 0;
        uint8_t page = (uint8_t)((i % 4) * 2);
        uint8_t col = i < 4 ? 1 : 63;
        oled_print(page, col, fields[i]);
        assert(command_count == 3 && commands[0] == 0xB0 + page);
        assert(commands[1] == (col & 15) && commands[2] == 0x10 + (col >> 4));
        assert(pixel_columns == 60 && col + pixel_columns <= 128);
    }
    state.light.system_level_percent = 0;
    state.light.mod = LIGHT_MOD_DAWN;
    display_format_snapshot(&state, fields);
    assert(strcmp(fields[4], "L-S:0%    ") == 0); // Erases old 100% suffix.
    assert(strcmp(fields[7], "MOD:DAWN  ") == 0);
    puts("OLED formatting, font bounds, and original command sequence tests passed");
    return 0;
}
