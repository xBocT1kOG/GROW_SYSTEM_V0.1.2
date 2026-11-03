#include "display_task.h"
#include "display_format.h"
#include "../app/app_control.h"
#include "../config/app_config.h"
#include "driver/i2c_master.h"
#include "freertos/task.h"
#include "esp_log.h"

// ---------------------------------------------------------
// BOUNDED TRANSPORT FOR THE UNCHANGED CUSTOM RENDERER
// ---------------------------------------------------------
static esp_err_t display_error;
static TaskHandle_t task_handle;
static const char *TAG = "display_task";

static esp_err_t display_transmit(i2c_master_dev_handle_t device, const uint8_t *data,
                                  size_t length, int legacy_timeout)
{
    (void)legacy_timeout;
    if (display_error == ESP_OK) {
        display_error = i2c_master_transmit(device, data, length, 100);
    }
    return display_error; // Skip remaining frame writes after the first failure.
}

// Keep all register bytes, addressing, glyphs, and clear/print sequences intact.
#define i2c_master_transmit display_transmit
#include "../SSD1306_CustomLib.h"
#undef i2c_master_transmit

// ---------------------------------------------------------
// DISPLAY RENDERING
// ---------------------------------------------------------
static void render_snapshot(const app_state_t *snapshot)
{
    char fields[DISPLAY_FIELD_COUNT][DISPLAY_CELL_CHARS + 1];
    display_format_snapshot(snapshot, fields);
    for (unsigned row = 0; row < 4; ++row) {
        oled_print((uint8_t)(row * 2), 1, fields[row]);
        oled_print((uint8_t)(row * 2), 63, fields[row + 4]);
    }
}

static void display_task(void *argument)
{
    (void)argument;
    // Preserve the proven startup screen and delays.
    vTaskDelay(pdMS_TO_TICKS(1000));
    oled_init();
    oled_clear();
    if (display_error == ESP_OK) {
        oled_print(3, 3, "GROW SYSTEM V0.1.2");
        vTaskDelay(pdMS_TO_TICKS(1000));
        oled_print(5, 3, "MADE BY HUP&KIRILL");
        vTaskDelay(pdMS_TO_TICKS(3000));
        oled_clear();
    }
    if (display_error != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(display_error));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Display task started");
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        app_state_t snapshot;
        esp_err_t err = app_state_get_snapshot(&snapshot);
        if (err == ESP_OK) render_snapshot(&snapshot);
        if (err != ESP_OK || display_error != ESP_OK) {
            ESP_LOGE(TAG, "OLED stopped: %s",
                     esp_err_to_name(err != ESP_OK ? err : display_error));
            vTaskDelete(NULL); // Output failure never stops application control.
            return;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
    }
}

// ---------------------------------------------------------
// STARTUP
// ---------------------------------------------------------
esp_err_t display_task_start(void)
{
    if (task_handle != NULL) return ESP_ERR_INVALID_STATE;
    if (oled_handle == NULL) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(display_task, "display_task", DISPLAY_TASK_STACK_BYTES, NULL,
                    DISPLAY_TASK_PRIORITY, &task_handle) != pdPASS) {
        task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
