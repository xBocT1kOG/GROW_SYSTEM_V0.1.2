#include "pump.h"
#include "../config/board_config.h"

// ---------------------------------------------------------
// PRIVATE DRIVER STATE
// ---------------------------------------------------------
static bool initialized;
static bool last_enabled;

// ---------------------------------------------------------
// PUMP INITIALIZATION
// ---------------------------------------------------------
esp_err_t pump_init(void)
{
    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = gpio_reset_pin(PUMP_PIN);
    if (err != ESP_OK) {
        return err;
    }
    // Load the OFF latch before enabling the active-high output.
    err = gpio_set_level(PUMP_PIN, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_direction(PUMP_PIN, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        return err;
    }

    last_enabled = false;
    initialized = true;
    return ESP_OK;
}

// ---------------------------------------------------------
// PUMP OUTPUT
// ---------------------------------------------------------
esp_err_t pump_set(bool enabled)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = gpio_set_level(PUMP_PIN, enabled ? 1 : 0);
    if (err != ESP_OK) {
        return err;
    }
    last_enabled = enabled;
    return ESP_OK;
}

bool pump_get(void)
{
    return last_enabled;
}
