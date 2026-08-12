#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "nvs_flash.h"       // Энергонезависимая память (нужна для Wi-Fi)
#include "esp_wifi.h"        // Драйвер Wi-Fi
#include "esp_event.h"       // Обработчик системных событий
#include "esp_http_client.h" // HTTP клиент
#include "driver/gpio.h"
#include "esp_timer.h"       // Для точного отсчета времени
#include "rom/ets_sys.h"     // Для функции задержки ets_delay_us

// Тег для красивого вывода в лог
static const char *TAG = "GROW_HW_TEST";

// --- НАСТРОЙКИ Wi-Fi & BLYNK ---
#define WIFI_SSID "xBocT1k_2.4G"
#define WIFI_PASS "******" // Ваш пароль Wi-Fi
#define BLYNK_TOKEN "******"  // Ваш токен Blynk

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

// Наш цифровой пин для насоса
#define PUMP_PIN 13 

// Пины для ультразвукового датчика (HC-SR04)
#define TRIG_PIN 14
#define ECHO_PIN 2

// Константы для калибровки уровня воды (в сантиметрах)
#define DISTANCE_EMPTY_CM 16.0f
#define DISTANCE_FULL_CM 7.0f

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
    ets_delay_us(50000); // Используем функцию задержки из ROM
    
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
// ОБРАБОТЧИК И ИНИЦИАЛИЗАЦИЯ WI-FI
// ---------------------------------------------------------
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Переподключение к Wi-Fi...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi подключен! IP получен.");
    }
}

void wifi_init(void) {
    ESP_ERROR_CHECK(nvs_flash_init()); // NVS обязателен для работы Wi-Fi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// ---------------------------------------------------------
// ОТПРАВКА ДАННЫХ В BLYNK (HTTP REST API)
// ---------------------------------------------------------
void send_to_blynk(int temp, int hum, int cms_raw, int water_percentage) {
    char url[256];
    // Собираем строку запроса с нашими переменными
    snprintf(url, sizeof(url), "http://blynk.cloud/external/api/batch/update?token=%s&V0=%d&V1=%d&V2=%d&V4=%d", 
             BLYNK_TOKEN, temp, hum, cms_raw, water_percentage);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Успех BLYNK: T=%d, H=%d, CMS=%d, Water=%d", temp, hum, cms_raw, water_percentage);
    } else {
        ESP_LOGE(TAG, "Ошибка отправки: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

// ---------------------------------------------------------
// ЧТЕНИЕ ДАННЫХ ИЗ BLYNK (Кнопка насоса) - ИСПРАВЛЕННОЕ
// ---------------------------------------------------------
int read_blynk_button(void) {
    char url[256];
    snprintf(url, sizeof(url), "http://blynk.cloud/external/api/get?token=%s&V3", BLYNK_TOKEN);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    int button_state = 0;
    
    // 1. Открываем соединение (0 - означает стандартную длину)
    if (esp_http_client_open(client, 0) == ESP_OK) {
        
        // 2. Получаем системные заголовки от сервера
        esp_http_client_fetch_headers(client);
        
        // 3. Читаем само тело ответа
        char response[16] = {0};
        int read_bytes = esp_http_client_read(client, response, sizeof(response) - 1);
        
        if (read_bytes > 0) {            
            // Проверяем наличие единицы в ответе
            if (strchr(response, '1') != NULL) {
                button_state = 1;
            }
        } else {
            ESP_LOGW(TAG, "Сервер ответил, но тело ответа пустое");
        }
    } else {
        ESP_LOGE(TAG, "Не удалось открыть HTTP соединение для чтения");
    }
    
    // 4. Обязательно закрываем поток и очищаем память
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    
    return button_state;
}

// ---------------------------------------------------------
// ИНИЦИАЛИЗАЦИЯ УЛЬТРАЗВУКОВОГО ДАТЧИКА
// ---------------------------------------------------------
void init_ultrasonic(void) {
    // Настраиваем TRIG как выход
    gpio_reset_pin(TRIG_PIN);
    gpio_set_direction(TRIG_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(TRIG_PIN, 0); // По умолчанию выключен

    // Настраиваем ECHO как вход
    gpio_reset_pin(ECHO_PIN);
    gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);
}

// ---------------------------------------------------------
// ФУНКЦИЯ ИЗМЕРЕНИЯ ДИСТАНЦИИ УЛЬТРАЗВУКОМ
// ---------------------------------------------------------
float get_water_distance_cm(void) {
    // 1. Посылаем стартовый импульс длиной 10 микросекунд
    gpio_set_level(TRIG_PIN, 1);
    ets_delay_us(10);
    gpio_set_level(TRIG_PIN, 0);

    // 2. Ждем, пока датчик поднимет ECHO в HIGH (начало ответа)
    int64_t start_wait = esp_timer_get_time();
    while (gpio_get_level(ECHO_PIN) == 0) {
        // Таймаут, чтобы код не завис, если датчик отключится
        if (esp_timer_get_time() - start_wait > 20000) {
            ESP_LOGE(TAG, "Таймаут датчика уровня: нет ответа");
            return -1.0; 
        }
    }
    int64_t echo_start = esp_timer_get_time(); // Фиксируем время старта

    // 3. Ждем, пока датчик опустит ECHO в LOW (конец ответа)
    while (gpio_get_level(ECHO_PIN) == 1) {
        if (esp_timer_get_time() - echo_start > 30000) { // Примерно 5 метров
            ESP_LOGE(TAG, "Таймаут датчика уровня: сигнал потерян");
            return -1.0;
        }
    }
    int64_t echo_end = esp_timer_get_time(); // Фиксируем время возврата

    // 4. Считаем дистанцию
    int64_t duration = echo_end - echo_start;
    float distance_cm = (duration * 0.0343) / 2.0;
    
    return distance_cm;
}

// Функция сортировки для фильтра
int compare_floats(const void *a, const void *b) {
    float fa = *(const float*) a;
    float fb = *(const float*) b;
    return (fa > fb) - (fa < fb);
}

// Новая функция, которую нужно вызывать в главном цикле
float get_stable_water_level(void) {
    const int NUM_READINGS = 20;
    float readings[NUM_READINGS];
    int valid_count = 0;

    // Делаем 20 быстрых замеров
    for (int i = 0; i < NUM_READINGS; i++) {
        float dist = get_water_distance_cm(); // Твоя базовая функция
        if (dist > 0 && dist < 400) {         // Отсекаем явный бред (таймауты)
            readings[valid_count] = dist;
            valid_count++;
        }
        vTaskDelay(pdMS_TO_TICKS(100));        // Обязательная пауза 100мс, чтобы звук в баке утих!
    }

    // Если всё плохо и датчик отвалился
    if (valid_count == 0) return -1.0; 
    
    // Если есть данные - сортируем их
    qsort(readings, valid_count, sizeof(float), compare_floats);

    // Возвращаем значение из середины отсортированного списка
    return readings[valid_count / 2]; 
}

// Функция маппинга
int get_water_percentage(float current_distance) {
    // 1. Ограничиваем (clamp) значения, чтобы не вылезти за 0-100%
    if (current_distance > DISTANCE_EMPTY_CM) {
        current_distance = DISTANCE_EMPTY_CM;
    }
    if (current_distance < DISTANCE_FULL_CM) {
        current_distance = DISTANCE_FULL_CM;
    }

    // 2. Сама математика (инвертированная, так как чем меньше см, тем больше воды)
    // (16 - текущая_дистанция) / (16 - 6) * 100
    float percentage = ((DISTANCE_EMPTY_CM - current_distance) / (DISTANCE_EMPTY_CM - DISTANCE_FULL_CM)) * 100.0f;

    // 3. Округляем до математически правильного целого числа и возвращаем
    return (int)(percentage + 0.5f);
}

// ---------------------------------------------------------
// ТОЧКА ВХОДА (Главная программа)
// ---------------------------------------------------------
void app_main(void) {

    // Инициализация пина насоса
    gpio_reset_pin(PUMP_PIN);
    gpio_set_direction(PUMP_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PUMP_PIN, 0); // 0 - выключено, 1 - включено

    // Инициализация ультразвукового датчика
    init_ultrasonic();

    ESP_LOGI(TAG, "=== Запуск тестирования оборудования ===");

    // Инициализация NVS и Wi-Fi
    ESP_ERROR_CHECK(nvs_flash_init());
    nvs_flash_init();
    wifi_init();

    // Ждем 5 секунд, чтобы Wi-Fi успел подключиться
    vTaskDelay(pdMS_TO_TICKS(5000));

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
        .atten = ADC_ATTEN_DB_12, 
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHAN, &config));
    ESP_LOGI(TAG, "АЦП инициализирован на канале %d (GP4)", ADC_CHAN);

    ESP_LOGI(TAG, "Начинаем опрос сенсоров...\n");

    // 3. Бесконечный цикл опроса
    int tick_count = 0;

    while (1) {
        // 1. Опрашиваем кнопку из облака каждые 10 секунды
        int pump_on = read_blynk_button();
        
        // 2. Включаем или выключаем физический пин
        gpio_set_level(PUMP_PIN, pump_on);

        // 3. Читаем ультразвуковой датчик
        float water_distance = get_stable_water_level();

        if (water_distance > 0) {
        ESP_LOGI(TAG, "Расстояние до воды: %.2f см", water_distance);
            int water_percentage = get_water_percentage(water_distance);
            ESP_LOGI(TAG, "Уровень воды: %d%%", water_percentage);
        } else {
            ESP_LOGW(TAG, "Ошибка измерения расстояния до воды");
        }   
        
        // 4. Отправляем телеметрию раз в 10 секунд
        if (tick_count >= 2) { // 2 * 5 секунд = 10 секунд
            int temp = read_htu21d(HTU21D_TEMP_CMD); // твой код чтения 
            int hum = read_htu21d(HTU21D_HUMIDITY_CMD);
            int cms_raw = 0;
            int water_percentage = get_water_percentage(water_distance);
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHAN, &cms_raw));

            send_to_blynk(temp, hum, cms_raw, water_percentage);
            tick_count = 0; // Сбрасываем счетчик
        }

        tick_count++;
        vTaskDelay(pdMS_TO_TICKS(5000)); // Базовая пауза цикла 5 секунды
    }
}