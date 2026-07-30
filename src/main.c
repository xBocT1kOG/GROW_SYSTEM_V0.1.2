#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

// Тег для красивого вывода в лог
static const char *TAG = "GROW_HW_TEST";

// --- НАСТРОЙКИ ПИНОВ ---
#define I2C_MASTER_SDA_IO       11      // GP11
#define I2C_MASTER_SCL_IO       12      // GP12
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      100000  // Стандартная скорость I2C
#define I2C_MASTER_TIMEOUT_MS   1000

#define ADC_CHAN                ADC_CHANNEL_3 // GP4 соответствует ADC1_CH3

// --- КОМАНДЫ GY-21 (HTU21D) ---
#define HTU21D_ADDR             0x40
#define HTU21D_TEMP_CMD         0xE3
#define HTU21D_HUMIDITY_CMD     0xE5

// ---------------------------------------------------------
// Функция инициализации I2C мастера
// ---------------------------------------------------------
static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// ---------------------------------------------------------
// Функция чтения данных с GY-21
// ---------------------------------------------------------
float read_htu21d(uint8_t cmd) {
    uint8_t data[3];
    
    // Отправляем команду на измерение
    i2c_master_write_to_device(I2C_MASTER_NUM, HTU21D_ADDR, &cmd, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    
    // Ждем 50мс, пока датчик сделает замер
    vTaskDelay(pdMS_TO_TICKS(50)); 
    
    // Читаем 3 байта (2 байта данных + 1 байт контрольной суммы)
    esp_err_t err = i2c_master_read_from_device(I2C_MASTER_NUM, HTU21D_ADDR, data, 3, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка чтения I2C!");
        return -999.0; // Возвращаем ошибку
    }
    
    // Склеиваем байты и зануляем биты статуса
    uint16_t raw_val = (data[0] << 8) | data[1];
    raw_val &= 0xFFFC; 
    
    // Конвертируем сырые данные по даташиту HTU21D
    if (cmd == HTU21D_TEMP_CMD) {
        return -46.85 + (175.72 * raw_val / 65536.0);
    } else {
        return -6.0 + (125.0 * raw_val / 65536.0);
    }
}

// ---------------------------------------------------------
// ТОЧКА ВХОДА (Главная программа)
// ---------------------------------------------------------
void app_main(void) {
    ESP_LOGI(TAG, "=== Запуск тестирования оборудования ===");

    // 1. Инициализация I2C (GY-21)
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "Шина I2C инициализирована на пинах %d (SDA) и %d (SCL)", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    // 2. Инициализация ADC1 (Емкостной датчик)
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // Настраиваем 11dB (позволяет читать напряжения от 0 до ~3.1V)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11, 
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHAN, &config));
    ESP_LOGI(TAG, "АЦП инициализирован на канале %d (GP4)", ADC_CHAN);

    ESP_LOGI(TAG, "Начинаем опрос сенсоров...\n");

    // 3. Бесконечный цикл опроса
    while (1) {
        // Читаем I2C
        int temp = read_htu21d(HTU21D_TEMP_CMD);
        int hum = read_htu21d(HTU21D_HUMIDITY_CMD);

        // Читаем АЦП
        int cms_raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHAN, &cms_raw));

        // Выводим в терминал (зеленым цветом)
        ESP_LOGI(TAG, "Т: %d °C | В: %d %% | CMS_RAW: %d", temp, hum, cms_raw);

        // Пауза 2 секунды. vTaskDelay обязателен, чтобы не повесить Watchdog
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}