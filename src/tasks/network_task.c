#include "network_task.h"
#include "network_sync.h"
#include "../services/network_service.h"
#include "../services/time_service.h"
#include "../app/app_control.h"
#include "../config/app_config.h"
#include "freertos/task.h"
#include "esp_log.h"

// ---------------------------------------------------------
// REMOTE CHANGE TRACKING
// ---------------------------------------------------------
static TaskHandle_t task_handle;
static const char *TAG = "network_task";
static network_sync_t known;

static int send_control(unsigned index, int remote, int local, app_event_type_t type)
{
    if (!network_sync_changed(&known, index, remote)) return 0;
    if (remote != local) {
        app_event_t event = { .type = type };
        if (index == 1) event.data.light_percent = (uint8_t)remote;
        else event.data.enabled = remote != 0;
        esp_err_t err = app_control_post_event(&event, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Remote command queue full");
            return -1; // Preserve baseline and block uploads until the next poll retries.
        }
    }
    network_sync_observe(&known, index, remote);
    return remote != local;
}

// ---------------------------------------------------------
// INDEPENDENT TELEMETRY AND CONTROL PERIODS
// ---------------------------------------------------------
static void network_task(void *argument)
{
    (void)argument;
    TickType_t last_poll = xTaskGetTickCount() - pdMS_TO_TICKS(BLYNK_CONTROL_POLL_MS);
    TickType_t last_upload = xTaskGetTickCount() - pdMS_TO_TICKS(BLYNK_TELEMETRY_PERIOD_MS);
    TickType_t last_connect = xTaskGetTickCount() - pdMS_TO_TICKS(5000);
    bool reported_connected = false;
    bool reported_sync = false;
    bool poll_ready = false;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        bool connected = network_service_connected();
        if (connected != reported_connected) {
            app_event_t event = { .type = connected ? APP_EVT_WIFI_CONNECTED : APP_EVT_WIFI_DISCONNECTED };
            if (app_control_post_event(&event, 0) == ESP_OK) {
                reported_connected = connected;
                ESP_LOGI(TAG, "Wi-Fi %s", connected ? "connected" : "disconnected");
            }
        }
        if (!reported_sync && time_service_now().synced) {
            const app_event_t event = { .type = APP_EVT_TIME_SYNCED };
            if (app_control_post_event(&event, 0) == ESP_OK) {
                reported_sync = true;
                ESP_LOGI(TAG, "Time synchronized");
            }
        }
        if (!connected) {
            poll_ready = false;
            if ((TickType_t)(now - last_connect) >= pdMS_TO_TICKS(5000)) {
                last_connect = now;
                network_service_reconnect();
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        bool commands_pending = false;
        if ((TickType_t)(now - last_poll) >= pdMS_TO_TICKS(BLYNK_CONTROL_POLL_MS)) {
            last_poll = now;
            network_controls_t remote;
            esp_err_t err = network_service_poll(&remote);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Blynk control poll failed: %s", esp_err_to_name(err));
                poll_ready = false;
            } else {
                app_state_t state;
                app_state_get_snapshot(&state);
                int pump = send_control(0, remote.pump, state.pump_enabled, APP_EVT_BLYNK_PUMP_SET);
                int light = send_control(1, remote.light, state.light.system_level_percent, APP_EVT_BLYNK_LIGHT_SET);
                int automatic = send_control(2, remote.automatic, state.light.auto_enabled, APP_EVT_BLYNK_AUTO_SET);
                poll_ready = pump >= 0 && light >= 0 && automatic >= 0;
                commands_pending = pump > 0 || light > 0 || automatic > 0;
            }
        }

        if (commands_pending) {
            // Give control_task an opportunity to publish command results before uploading.
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        now = xTaskGetTickCount();
        if (poll_ready && !commands_pending &&
            (TickType_t)(now - last_upload) >= pdMS_TO_TICKS(BLYNK_TELEMETRY_PERIOD_MS)) {
            last_upload = now;
            app_state_t state;
            app_state_get_snapshot(&state);
            esp_err_t err = network_service_upload(&state);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Blynk telemetry failed: %s", esp_err_to_name(err));
            } else {
                // Our state write is the next polling baseline, not a remote command.
                network_sync_observe(&known, 0, state.pump_enabled);
                network_sync_observe(&known, 1, state.light.system_level_percent);
                network_sync_observe(&known, 2, state.light.auto_enabled);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t network_task_start(const char *ssid, const char *password, const char *token)
{
    if (task_handle != NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t err = network_service_init(ssid, password, token);
    if (err != ESP_OK) return err;
    if (xTaskCreate(network_task, "network_task", NETWORK_TASK_STACK_BYTES, NULL,
                    NETWORK_TASK_PRIORITY, &task_handle) != pdPASS) {
        task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
