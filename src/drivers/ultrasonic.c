#include "ultrasonic.h"
#include <stdbool.h>
#include "../config/board_config.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

// ---------------------------------------------------------
// GPIO INITIALIZATION
// ---------------------------------------------------------
static bool initialized;

esp_err_t ultrasonic_init(void)
{
    if (initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = gpio_set_level(ULTRASONIC_TRIG_PIN, 0);
    if (err != ESP_OK) return err;
    const gpio_config_t trigger = {
        .pin_bit_mask = 1ULL << ULTRASONIC_TRIG_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&trigger);
    if (err != ESP_OK) return err;
    const gpio_config_t echo = {
        .pin_bit_mask = 1ULL << ULTRASONIC_ECHO_PIN,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&echo);
    if (err != ESP_OK) return err;
    initialized = true;
    return ESP_OK;
}

// ---------------------------------------------------------
// SINGLE ECHO MEASUREMENT
// ---------------------------------------------------------
esp_err_t ultrasonic_read(float *distance_cm)
{
    if (distance_cm == NULL) return ESP_ERR_INVALID_ARG;
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (gpio_get_level(ULTRASONIC_ECHO_PIN) != 0) return ESP_ERR_INVALID_STATE;
    esp_err_t err = gpio_set_level(ULTRASONIC_TRIG_PIN, 1);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(10);
    err = gpio_set_level(ULTRASONIC_TRIG_PIN, 0);
    if (err != ESP_OK) return err;

    int64_t start = esp_timer_get_time();
    while (gpio_get_level(ULTRASONIC_ECHO_PIN) == 0) {
        if (esp_timer_get_time() - start >= 20000) return ESP_ERR_TIMEOUT;
    }
    const int64_t rise = esp_timer_get_time();
    while (gpio_get_level(ULTRASONIC_ECHO_PIN) != 0) {
        if (esp_timer_get_time() - rise >= 30000) return ESP_ERR_TIMEOUT;
    }
    const float distance = (esp_timer_get_time() - rise) * 0.0343f / 2.0f;
    if (distance <= 0 || distance > 400) return ESP_ERR_INVALID_RESPONSE;
    *distance_cm = distance;
    return ESP_OK;
}
