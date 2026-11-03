#include "htu21.h"
#include "../config/board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------------------------------------------------------
// DEVICE INITIALIZATION
// ---------------------------------------------------------
static i2c_master_dev_handle_t device;

esp_err_t htu21_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) return ESP_ERR_INVALID_ARG;
    if (device != NULL) return ESP_ERR_INVALID_STATE;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = HTU21_I2C_ADDRESS,
        .scl_speed_hz = I2C_CLOCK_HZ,
    };
    return i2c_master_bus_add_device(bus, &config, &device);
}

// ---------------------------------------------------------
// CHECKED NO-HOLD MEASUREMENTS
// ---------------------------------------------------------
static esp_err_t read_raw(uint8_t command, uint16_t *raw)
{
    esp_err_t err = i2c_master_transmit(device, &command, 1, 100);
    if (err != ESP_OK) return err;
    // Legacy conversion sequence; allow a tick of margin beyond 50 ms.
    vTaskDelay(pdMS_TO_TICKS(60));
    uint8_t data[3];
    err = i2c_master_receive(device, data, sizeof(data), 100);
    if (err != ESP_OK) return err;
    uint8_t crc = 0;
    for (unsigned i = 0; i < 2; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    if (crc != data[2]) return ESP_ERR_INVALID_CRC;
    *raw = ((uint16_t)data[0] << 8) | (data[1] & 0xFC);
    return ESP_OK;
}

esp_err_t htu21_read(float *temperature_c, float *humidity_percent)
{
    if (temperature_c == NULL || humidity_percent == NULL) return ESP_ERR_INVALID_ARG;
    if (device == NULL) return ESP_ERR_INVALID_STATE;
    uint16_t temperature_raw, humidity_raw;
    esp_err_t err = read_raw(0xF3, &temperature_raw);
    if (err != ESP_OK) return err;
    err = read_raw(0xF5, &humidity_raw);
    if (err != ESP_OK) return err;
    const float temperature = -46.85f + 175.72f * temperature_raw / 65536.0f;
    float humidity = -6.0f + 125.0f * humidity_raw / 65536.0f;
    if (temperature < -40.0f || temperature > 125.0f) return ESP_ERR_INVALID_RESPONSE;
    if (humidity < 0) humidity = 0;
    if (humidity > 100) humidity = 100;
    *temperature_c = temperature;
    *humidity_percent = humidity;
    return ESP_OK;
}
