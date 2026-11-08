#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"

// ---------------------------------------------------------
// HTU21 API
// ---------------------------------------------------------
// Reuse the existing bus; creates only the HTU21 device.
esp_err_t htu21_init(i2c_master_bus_handle_t bus);
// Single sensor-task caller; outputs change only on complete success.
esp_err_t htu21_read(float *temperature_c, float *humidity_percent);
