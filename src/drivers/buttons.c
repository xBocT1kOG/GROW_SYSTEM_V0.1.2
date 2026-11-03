#include "buttons.h"
#include <stdint.h>
#include "../config/board_config.h"

// ---------------------------------------------------------
// GPIO EDGE NOTIFICATIONS
// ---------------------------------------------------------
static const gpio_num_t button_pins[BUTTON_COUNT] = {
    [BUTTON_PUMP] = PUMP_BUTTON_PIN,
    [BUTTON_AUTO] = AUTOLIGHT_BUTTON_PIN,
    [BUTTON_LIGHT_PLUS] = LIGHT_PLUS_BUTTON_PIN,
    [BUTTON_LIGHT_MINUS] = LIGHT_MINUS_BUTTON_PIN,
};
static TaskHandle_t input_task_handle;
static bool initialized;

static void button_edge_isr(void *argument)
{
    const button_id_t button = (button_id_t)(uintptr_t)argument;
    BaseType_t task_woken = pdFALSE;
    xTaskNotifyFromISR(input_task_handle, 1U << button, eSetBits, &task_woken);
    if (task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

// ---------------------------------------------------------
// GPIO INITIALIZATION
// ---------------------------------------------------------
esp_err_t buttons_init(TaskHandle_t task)
{
    if (task == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Reject legacy service ownership before changing any pin configuration.
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK) {
        return err;
    }
    input_task_handle = task;

    uint64_t pin_mask = 0;
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        pin_mask |= 1ULL << button_pins[i];
    }
    const gpio_config_t config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, // External 10k pull-ups
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    err = gpio_config(&config);
    if (err != ESP_OK) {
        goto fail;
    }
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        err = gpio_isr_handler_add(button_pins[i], button_edge_isr,
                                   (void *)(uintptr_t)i);
        if (err != ESP_OK) {
            goto fail;
        }
    }
    initialized = true;
    return ESP_OK;

fail:
    // Removes partial handlers before the caller can delete its task.
    gpio_uninstall_isr_service();
    input_task_handle = NULL;
    return err;
}

bool buttons_is_pressed(button_id_t button)
{
    if (!initialized || button < 0 || button >= BUTTON_COUNT) {
        return false;
    }
    return gpio_get_level(button_pins[button]) == 0; // Active LOW
}
