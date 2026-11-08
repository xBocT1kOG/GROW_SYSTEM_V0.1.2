#pragma once
#include "../app/app_state.h"

typedef struct {
    bool pump;
    uint8_t light;
    bool automatic;
} network_controls_t;

// ---------------------------------------------------------
// WIFI AND BLYNK SERVICE
// ---------------------------------------------------------
esp_err_t network_service_init(const char *ssid, const char *password, const char *token);
bool network_service_connected(void);
void network_service_reconnect(void);
esp_err_t network_service_poll(network_controls_t *controls);
esp_err_t network_service_upload(const app_state_t *snapshot);
