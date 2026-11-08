#pragma once
#include "esp_err.h"

// Start once after control startup; failure must not abort local operation.
esp_err_t network_task_start(const char *ssid, const char *password, const char *token);
