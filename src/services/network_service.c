#include "network_service.h"
#include "network_protocol.h"
#include "time_service.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "cJSON.h"

// ---------------------------------------------------------
// WIFI LIFECYCLE
// ---------------------------------------------------------
static atomic_bool connected;
static bool initialized;
static char device_token[129];
static const char *TAG = "network_service";

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&connected, true);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&connected, false);
    }
}

bool network_service_connected(void)
{
    return atomic_load(&connected);
}

void network_service_reconnect(void)
{
    if (!network_service_connected()) {
        // Retry only at the normal network period, including startup failures.
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
            ESP_LOGW(TAG, "Wi-Fi connect failed: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t network_service_init(const char *ssid, const char *password, const char *token)
{
    if (initialized) return ESP_ERR_INVALID_STATE;
    if (!ssid || !password || !token || strlen(ssid) > 32 ||
        strlen(password) > 64 || strlen(token) == 0 || strlen(token) >= sizeof(device_token)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (const char *p = token; *p; ++p) {
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') return ESP_ERR_INVALID_ARG;
    }
    strcpy(device_token, token);
    // HTTP library debug messages may include query URLs containing credentials.
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err; // Never erase user NVS to recover automatically.
    err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    if (esp_netif_create_default_wifi_sta() == NULL) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL);
    if (err != ESP_OK) return err;
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    // Initialize time and SNTP only after the TCP/IP stack is ready.
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();
    time_service_init();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    initialized = true;
    return ESP_OK;
}

// ---------------------------------------------------------
// BOUNDED HTTPS TRANSPORT
// ---------------------------------------------------------
typedef struct {
    char data[512];
    size_t length;
    bool overflow;
} response_t;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_t *response = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA) {
        if (event->data_len < 0 || response->length + (size_t)event->data_len >= sizeof(response->data)) {
            response->overflow = true;
            return ESP_FAIL;
        }
        memcpy(response->data + response->length, event->data, event->data_len);
        response->length += event->data_len;
        response->data[response->length] = 0;
    }
    return ESP_OK;
}

static esp_err_t request(const char *url, response_t *response)
{
    if (!network_service_connected()) return ESP_ERR_INVALID_STATE;
    const esp_http_client_config_t config = {
        .url = url, .method = HTTP_METHOD_GET, .timeout_ms = 2000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
        .event_handler = http_event, .user_data = response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK && (esp_http_client_get_status_code(client) != 200 || response->overflow)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    esp_http_client_cleanup(client);
    return err; // Never log URLs, tokens, response bodies, or credentials.
}

// ---------------------------------------------------------
// BLYNK CONTROL POLLING AND BATCH TELEMETRY
// ---------------------------------------------------------
esp_err_t network_service_poll(network_controls_t *controls)
{
    if (!controls) return ESP_ERR_INVALID_ARG;
    char url[256];
    snprintf(url, sizeof(url), "https://blynk.cloud/external/api/get?token=%s&v3&v5&v7", device_token);
    response_t response = {0};
    esp_err_t err = request(url, &response);
    if (err != ESP_OK) return err;
    if (!network_parse_controls(response.data, response.length, controls)) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

esp_err_t network_service_upload(const app_state_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    char url[512];
    int used = snprintf(url, sizeof(url),
        "https://blynk.cloud/external/api/batch/update?token=%s&v3=%u&v5=%u&v7=%u&v8=%u",
        device_token, s->pump_enabled ? 1U : 0U, (unsigned)s->light.system_level_percent,
        s->light.auto_enabled ? 1U : 0U, (unsigned)s->light.mod);
    // Omit unavailable/stale sensors instead of uploading invented zero values.
    if (s->sensors.htu21_valid && s->sensors.htu21_error == ESP_OK) {
        used += snprintf(url + used, sizeof(url) - used, "&v0=%.2f&v1=%.2f",
                         (double)s->sensors.temperature_c, (double)s->sensors.humidity_percent);
    }
    if (s->sensors.soil_valid && s->sensors.soil_error == ESP_OK)
        used += snprintf(url + used, sizeof(url) - used, "&v2=%u", (unsigned)s->sensors.soil_percent);
    if (s->sensors.water_valid && s->sensors.water_error == ESP_OK)
        used += snprintf(url + used, sizeof(url) - used, "&v4=%u", (unsigned)s->sensors.water_percent);
    if (s->sensors.ldr_valid && s->sensors.ldr_error == ESP_OK)
        used += snprintf(url + used, sizeof(url) - used, "&v6=%u", (unsigned)s->light.actual_percent);
    if (used < 0 || used >= sizeof(url)) return ESP_ERR_INVALID_SIZE;
    response_t response = {0};
    return request(url, &response);
}
