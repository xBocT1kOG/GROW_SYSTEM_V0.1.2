#include "../diagnostics/diagnostics.h"
#include "input_task.h"
#include "../app/app_control.h"
#include "../config/app_config.h"
#include "../drivers/buttons.h"
#include "esp_log.h"

// ---------------------------------------------------------
// DEBOUNCE STATE
// ---------------------------------------------------------
#define INPUT_START_BIT (1UL << 31) // Separate from the four ISR edge bits

typedef struct {
    bool candidate_pressed;
    bool stable_pressed;
    TickType_t changed_at;
} button_debounce_t;

static TaskHandle_t input_handle;
static const char *TAG = "input_task";
static const app_event_type_t press_events[BUTTON_COUNT] = {
    [BUTTON_PUMP] = APP_EVT_PUMP_TOGGLE,
    [BUTTON_AUTO] = APP_EVT_LIGHT_AUTO_TOGGLE,
    [BUTTON_LIGHT_PLUS] = APP_EVT_LIGHT_INCREMENT,
    [BUTTON_LIGHT_MINUS] = APP_EVT_LIGHT_DECREMENT,
};

// Round up to keep short intervals nonzero at a low RTOS tick rate.
static TickType_t interval_ticks(uint32_t milliseconds)
{
    return (TickType_t)((milliseconds * configTICK_RATE_HZ + 999U) / 1000U);
}

static bool sample_buttons(button_debounce_t *states, uint32_t edges)
{
    const TickType_t now = xTaskGetTickCount();
    bool active = false;
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        button_debounce_t *state = &states[i];
        const bool pressed = buttons_is_pressed((button_id_t)i);
        if (pressed != state->candidate_pressed) {
            state->candidate_pressed = pressed;
            state->changed_at = now;
        } else if (pressed && !state->stable_pressed && (edges & (1U << i))) {
            state->changed_at = now; // A new falling edge restarts press confirmation.
        }

        if (state->candidate_pressed != state->stable_pressed &&
            (TickType_t)(now - state->changed_at) >= interval_ticks(BUTTON_DEBOUNCE_MS)) {
            state->stable_pressed = state->candidate_pressed;
            if (state->stable_pressed) {
                diagnostics_increment(DIAG_button_events);
                const app_event_t event = { .type = press_events[i] };
                const esp_err_t err = app_control_post_event(&event, 0);
                if (err != ESP_OK) {
                    // Drop explicitly; never block debounce or repeat a held press.
                    ESP_LOGW(TAG, "Button %d event dropped: %s", i, esp_err_to_name(err));
                }
            }
        }
        active |= state->candidate_pressed || state->stable_pressed;
    }
    return active;
}

// ---------------------------------------------------------
// INPUT EVENT PRODUCER
// ---------------------------------------------------------
static void input_task(void *argument)
{
    (void)argument;
    diagnostics_sample_stack(DIAG_TASK_input);
    uint32_t notifications = 0;
    // GPIO initialization must finish before sampling starts.
    do {
        xTaskNotifyWait(0, UINT32_MAX, &notifications, portMAX_DELAY);
    } while (!(notifications & INPUT_START_BIT));

    button_debounce_t states[BUTTON_COUNT] = {0};
    for (;;) {
        diagnostics_sample_stack(DIAG_TASK_input);
        const bool active = sample_buttons(states, notifications);
        notifications = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notifications,
                       active ? interval_ticks(BUTTON_SAMPLE_PERIOD_MS) : portMAX_DELAY);
    }
}

// ---------------------------------------------------------
// STARTUP
// ---------------------------------------------------------
esp_err_t input_task_start(void)
{
    if (input_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(input_task, "input_task", INPUT_TASK_STACK_BYTES, NULL,
                    INPUT_TASK_PRIORITY, &input_handle) != pdPASS) {
        input_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t err = buttons_init(input_handle);
    if (err != ESP_OK) {
        vTaskDelete(input_handle);
        input_handle = NULL;
        return err;
    }
    xTaskNotify(input_handle, INPUT_START_BIT, eSetBits);
    return ESP_OK;
}
