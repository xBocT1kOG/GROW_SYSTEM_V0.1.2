#include "light_pwm.h"
#include <stdbool.h>
#include "../config/board_config.h"

// ---------------------------------------------------------
// PRIVATE DRIVER STATE
// ---------------------------------------------------------
static bool initialized;
static uint8_t last_percent;

// ---------------------------------------------------------
// PWM INITIALIZATION
// ---------------------------------------------------------
esp_err_t light_pwm_init(void)
{
    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const ledc_timer_config_t timer = {
        .speed_mode = LIGHT_PWM_MODE,
        .timer_num = LIGHT_PWM_TIMER,
        .duty_resolution = LIGHT_PWM_RESOLUTION,
        .freq_hz = LIGHT_PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t channel = {
        .speed_mode = LIGHT_PWM_MODE,
        .channel = LIGHT_PWM_CHANNEL,
        .timer_sel = LIGHT_PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LIGHT_PWM_GPIO,
        .duty = 0, // Attach the output with the lamp off.
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) {
        return err;
    }

    last_percent = 0;
    initialized = true;
    return ESP_OK;
}

// ---------------------------------------------------------
// PWM OUTPUT
// ---------------------------------------------------------
esp_err_t light_pwm_set_percent(uint8_t percent)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (percent > 100) {
        percent = 100;
    }

    const uint32_t max_duty = (1U << LIGHT_PWM_RESOLUTION) - 1U;
    const uint32_t duty = (uint32_t)percent * max_duty / 100U; // 8-bit: 0..255
    esp_err_t err = ledc_set_duty(LIGHT_PWM_MODE, LIGHT_PWM_CHANNEL, duty);
    if (err != ESP_OK) {
        return err;
    }
    err = ledc_update_duty(LIGHT_PWM_MODE, LIGHT_PWM_CHANNEL);
    if (err != ESP_OK) {
        return err;
    }

    last_percent = percent;
    return ESP_OK;
}

uint8_t light_pwm_get_percent(void)
{
    return last_percent;
}
