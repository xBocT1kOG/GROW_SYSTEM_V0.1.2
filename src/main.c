#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "nvs_flash.h"       // Энергонезависимая память (нужна для Wi-Fi)
#include "esp_wifi.h"        // Драйвер Wi-Fi
#include "esp_event.h"       // Обработчик системных событий
#include "esp_http_client.h" // HTTP клиент
#include "driver/gpio.h"
#include "esp_timer.h"       // Для точного отсчета времени
#include "rom/ets_sys.h"     // Для функции задержки ets_delay_us
#include "driver/ledc.h"
#include "esp_log.h" 
#include "SSD1306_CustomLib.h"
#include <time.h>
#include "esp_sntp.h"
#include "freertos/queue.h"
#include "esp_timer.h"

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
#define GY21_ADDR               0x40    // Адрес датчика GY-21 (HTU21D)
#define ADC_CHAN                ADC_CHANNEL_3 // GP4 соответствует ADC1_CH3
#define PUMP_BUTTON_PIN              18 // GP18 - пин для кнопки

// Наш цифровой пин для насоса
#define PUMP_PIN 13 

// Пины для ультразвукового датчика (HC-SR04)
#define TRIG_PIN 14
#define ECHO_PIN 2

// Константы для калибровки уровня воды (в сантиметрах)
#define DISTANCE_EMPTY_CM 16.0f
#define DISTANCE_FULL_CM 7.0f

// --- НАСТРОЙКИ ШИМ ---
#define PWM_TIMER              LEDC_TIMER_0
#define PWM_MODE               LEDC_LOW_SPEED_MODE
#define PWM_OUTPUT_IO          (17)                // Замени на тот GPIO, куда подключишь конвертер
#define PWM_CHANNEL            LEDC_CHANNEL_0
#define PWM_DUTY_RES           LEDC_TIMER_8_BIT   // Разрешение 8 бит (диапазон 0 - 255)
#define PWM_FREQUENCY          (1000)             // 1 кГц обычно идеально для таких конвертеров

// Хэндлы для управления шиной и конкретным устройством на ней
i2c_master_bus_handle_t bus_handle;
i2c_master_dev_handle_t gy21_handle;
i2c_master_dev_handle_t oled_handle;
QueueHandle_t button_queue;

// Глобальные переменные
volatile float temp = 0;
volatile float hum = 0;
volatile int cms_raw = 0;
volatile int water_percentage = 0;
volatile int max_light = 6; // Максимальный уровень освещения (0-10), по умолчанию 6
volatile int sunrise_start = 6; // Время начала рассвета (6 утра)
volatile int sunset_start = 20; // Время начала заката (20 вечера)
const char * volatile current_mode = "IDLE"; // Текущий режим работы
volatile int current_light_level = 0; // Текущий уровень освещения (0-10)
volatile int sun_period = 1200000; // Период проверки времени (20 минут в миллисекундах)
volatile bool pump_state = false;

// ---------------------------------------------------------
// Функция инициализации I2C мастера
// ---------------------------------------------------------
void init_i2c_master(void) {
    // Настройка физической шины
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));

    // Добавление датчика GY-21 и OLED-дисплея на шину 
    i2c_device_config_t gy21_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GY21_ADDR,
        .scl_speed_hz = 100000, // Стандартная скорость 100 кГц
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &gy21_config, &gy21_handle));

    i2c_device_config_t oled_cfg = { 
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, 
        .device_address = 0x3C, 
        .scl_speed_hz = 100000 
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &oled_cfg, &oled_handle));
}

// ---------------------------------------------------------
// Функция чтения данных с GY-21
// ---------------------------------------------------------
void read_gy21_data(float *temperature, float *humidity) {
    uint8_t cmd_temp = 0xF3; 
    uint8_t cmd_hum = 0xF5;  
    uint8_t data[3];

    // Чтение температуры
    i2c_master_transmit(gy21_handle, &cmd_temp, 1, -1);
    vTaskDelay(pdMS_TO_TICKS(50)); // Ожидание конвертации аппаратно
    i2c_master_receive(gy21_handle, data, 3, -1);
    
    // Сборка двух байт (MSB и LSB) и вычисление по даташиту
    uint16_t raw_temp = (data[0] << 8) | (data[1] & 0xFC);
    *temperature = -46.85 + (175.72 * raw_temp / 65536.0);

    // Чтение влажности
    i2c_master_transmit(gy21_handle, &cmd_hum, 1, -1);
    vTaskDelay(pdMS_TO_TICKS(50));
    i2c_master_receive(gy21_handle, data, 3, -1);

    uint16_t raw_hum = (data[0] << 8) | (data[1] & 0xFC);
    *humidity = -6.0 + (125.0 * raw_hum / 65536.0);
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

    // --- Инициализация службы времени (NTP) ---
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Установка локального часового пояса с автоматическим переходом на летнее/зимнее время
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();
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
// ОТПРАВКА ДАННЫХ В BLYNK (HTTP REST API) - ПОЛЗУНОК
// ---------------------------------------------------------
void send_to_blynk_slyder_button(int current_light_level) {
    char url[256];
    // Собираем строку запроса с нашими переменными
    snprintf(url, sizeof(url), "http://blynk.cloud/external/api/batch/update?token=%s&V5=%d", 
             BLYNK_TOKEN, current_light_level);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Успех BLYNK: V5=%d", current_light_level);
    } else {
        ESP_LOGE(TAG, "Ошибка отправки: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

// ---------------------------------------------------------
// ЧТЕНИЕ ДАННЫХ ИЗ BLYNK (Кнопка насоса) - ИСПРАВЛЕННОЕ
// ---------------------------------------------------------
int read_blynk_pump_button(void) {
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
        }
    } else {
        // Если нет интернета или сервер недоступен
        esp_err_t err = esp_http_client_perform(client);
        ESP_LOGW("BLYNK", "Ошибка получения состояния кнопки: %s", esp_err_to_name(err));
    }

    // 4. Обязательно очищаем память
    esp_http_client_cleanup(client);

    return button_state; // Вернет 1, если кнопка нажата, и 0, если выключена
}

// ---------------------------------------------------------
// ОТПРАВКА СОСТОЯНИЯ НАСОСА В BLYNK (HTTP REST API)
// ---------------------------------------------------------
void send_to_blynk_pump_state(int pump_state) {
    char url[256];
    // Собираем строку запроса с нашими переменными
    snprintf(url, sizeof(url), "http://blynk.cloud/external/api/batch/update?token=%s&V3=%d", 
             BLYNK_TOKEN, pump_state);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Успех BLYNK: V3=%d", pump_state);
    } else {
        ESP_LOGE(TAG, "Ошибка отправки: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
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

// 1. Инициализация ШИМ (вызвать один раз внутри app_main)
void init_pwm_converter(void) {
    // Настройка таймера
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = PWM_MODE,
        .timer_num        = PWM_TIMER,
        .duty_resolution  = PWM_DUTY_RES,
        .freq_hz          = PWM_FREQUENCY, 
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Настройка канала и привязка к пину
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = PWM_MODE,
        .channel        = PWM_CHANNEL,
        .timer_sel      = PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PWM_OUTPUT_IO,
        .duty           = 0, // При старте выдаем 0 (0 Вольт на выходе)
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    
    ESP_LOGI("PWM", "ШИМ конвертер инициализирован на пине %d", PWM_OUTPUT_IO);
}

// 2. Функция маппинга (вызывать при получении данных от ползунка Blynk)
void set_converter_level(int slider_value) {
    // Жестко ограничиваем входные данные (от 0 до 10)
    if (slider_value < 0) slider_value = 0;
    if (slider_value > 10) slider_value = 10;

    // Конвертируем уровень (0-10) в скважность (0-255)
    // Умножаем на 25.5 для равномерного шага
    uint32_t duty = (uint32_t)(slider_value * 25.5f);

    // Применяем новые настройки к железу
    ESP_ERROR_CHECK(ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(PWM_MODE, PWM_CHANNEL));
    
    ESP_LOGI("PWM", "Уровень: %d/10 -> Скважность: %lu/255", slider_value, (unsigned long)duty);
}

int read_blynk_slider(void) {
    char url[256];
    snprintf(url, sizeof(url), "http://blynk.cloud/external/api/get?token=%s&V5", BLYNK_TOKEN);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    int button_slider = 0;
    
    // 1. Открываем соединение (0 - означает стандартную длину)
    if (esp_http_client_open(client, 0) == ESP_OK) {
        
        // 2. Получаем системные заголовки от сервера
        esp_http_client_fetch_headers(client);
        
        // 3. Читаем само тело ответа
        char response[16] = {0};
        int read_bytes = esp_http_client_read(client, response, sizeof(response) - 1);
        
        if (read_bytes > 0) {            
            // Проверяем наличие числа в ответе
            button_slider = atoi(response); // Преобразуем строку в число
            if (button_slider < 0 || button_slider > 10) {
                ESP_LOGW("BLYNK", "Получено некорректное значение ползунка: %d", button_slider);
                button_slider = 0; // Сбрасываем до безопасного значения
        }
        }
    } else {
        // Если нет интернета или сервер недоступен
        esp_err_t err = esp_http_client_perform(client);
        ESP_LOGW("BLYNK", "Ошибка получения состояния кнопки: %s", esp_err_to_name(err));
    }

    // 4. Обязательно очищаем память
    esp_http_client_cleanup(client);

    return button_slider; // Вернет значение ползунка от 0 до 10
}

// Функция вывода данных на OLED-дисплей
void display_task(void *pvParameters) {
    char buffer[32];
    vTaskDelay(pdMS_TO_TICKS(1000)); // Ждем, чтобы шина I2C успела инициализироваться
    oled_init();
    oled_clear();

    oled_print(3, 3, "GROW SYSTEM V0.1.2");
    vTaskDelay(pdMS_TO_TICKS(1000));
    oled_print(5, 3, "MADE BY HUP&KIRILL");
    vTaskDelay(pdMS_TO_TICKS(3000));
    oled_clear();

    while (1) {

        snprintf(buffer, sizeof(buffer), "TEMP: %dC ", (int)temp);
        oled_print(0, 1, buffer);

        snprintf(buffer, sizeof(buffer), "HUM: %d%% ", (int)hum);
        oled_print(2, 1, buffer);

        snprintf(buffer, sizeof(buffer), "CMS: %d%%  ", cms_raw);
        oled_print(4, 1, buffer);

        snprintf(buffer, sizeof(buffer), "WATER: %d%%  ", water_percentage);
        oled_print(6, 1, buffer);

        snprintf(buffer, sizeof(buffer), "LIGHT: %d0%% ", current_light_level);
        oled_print(0, 63, buffer);

        snprintf(buffer, sizeof(buffer), "MODE: %s ", current_mode);
        oled_print(2, 63, buffer);

        snprintf(buffer, sizeof(buffer), "PUMP: %s ", pump_state ? "ON" : "OFF");
        oled_print(4, 63, buffer);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Функция получения текущего часа (0-23) с учетом локального часового пояса
int get_current_hour(void) {
    time_t now;
    struct tm timeinfo;
    
    // Получаем текущее системное время (в секундах)
    time(&now);
    
    // Преобразуем секунды в структуру с часами, минутами и днями 
    // с учетом локального часового пояса
    localtime_r(&now, &timeinfo);
    
    // tm_hour всегда возвращает int в диапазоне от 0 до 23
    return timeinfo.tm_hour;
}

void auto_light(void *pvParameters) {
    while (1) {
        int current_hour = get_current_hour(); // Получаем текущий час (0-23)
        current_light_level = read_blynk_slider(); // Получаем уровень освещения из Blynk

        // Если текущее время находится между рассветом и закатом
        if (current_hour >= sunrise_start && current_hour < sunset_start) {
            current_light_level = read_blynk_slider(); // Получаем уровень освещения из Blynk
            if (current_light_level < max_light) {
                current_light_level += 1; // Постепенно увеличиваем уровень освещения
                set_converter_level(current_light_level);
                send_to_blynk_slyder_button(current_light_level); // Отправляем обновленный уровень в Blynk
                ESP_LOGI(TAG, "Уровень освещения обновлен: %d", current_light_level);
                current_mode = "DAWN"; // Устанавливаем режим в "рассвет"
            }
            }
        // Если текущее время находится между закатом и рассветом
        else if (current_hour >= sunset_start || current_hour < sunrise_start) {
            current_light_level = read_blynk_slider(); // Получаем уровень освещения из Blynk
            if (current_light_level > 0) {
                current_light_level -= 1; // Постепенно уменьшаем уровень освещения
                set_converter_level(current_light_level);
                send_to_blynk_slyder_button(current_light_level); // Отправляем обновленный уровень в Blynk
                ESP_LOGI(TAG, "Уровень освещения обновлен: %d", current_light_level);
                current_mode = "DUSK"; // Устанавливаем режим в "закат"
                
            }
        }

        else {
            ESP_LOGI(TAG, "Текущее время вне диапазона рассвета и заката. Освещение не изменяется.");
            current_mode = "IDLE"; // Устанавливаем режим в "ожидание"
             
        }
        vTaskDelay(pdMS_TO_TICKS(sun_period)); // Ждем 20 минут
    }
}

// ---------------------------------------------------------
// ОБРАБОТЧИК ПРЕРЫВАНИЯ КНОПКИ (для антидребезга)
// ---------------------------------------------------------
static void IRAM_ATTR button_isr_handler(void* arg) {
    static uint64_t last_isr_time = 0;
    uint64_t current_time = esp_timer_get_time(); // Время в микросекундах

    // Игнорируем нажатия быстрее 1с (1 000 000 мкс) - антидребезг
    if (current_time - last_isr_time > 1000000) {
        int pin_num = (int)arg;
        // Отправляем номер пина в очередь, не блокируя процессор
        xQueueSendFromISR(button_queue, &pin_num, NULL); 
        last_isr_time = current_time;
    }
}

//---------------------------------------------------------
// ЗАДАЧА ОБРАБОТКИ НАЖАТИЯ КНОПКИ
//---------------------------------------------------------
void pump_button_task(void *pvParameters) {
    int received_pin;
    while (1) {
        // Задача спит, пока в очередь не придет сигнал от прерывания
        if (xQueueReceive(button_queue, &received_pin, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Кнопка нажата!");

            // Инвертируем локальное состояние
            pump_state = !pump_state; 

            // 1. TODO: Включить/выключить физический пин реле насоса
            gpio_set_level(PUMP_PIN, pump_state);

            // 2. TODO: Отправить новое состояние в Blynk
            send_to_blynk_pump_state(pump_state);
        }
    }
}

//---------------------------------------------------------
// ИНИЦИАЛИЗАЦИЯ КНОПКИ С ПРЕРЫВАНИЕМ
//---------------------------------------------------------
void pump_button_init(void) {
    // Создаем очередь на 10 элементов
    button_queue = xQueueCreate(10, sizeof(int));
    xTaskCreate(pump_button_task, "pump_button_task", 4096, NULL, 10, NULL);

    // Настраиваем пин кнопки (подтяжка к питанию, срабатывание по падению напряжения)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PUMP_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_NEGEDGE // Срабатывает при нажатии (замыкании на землю)
    };
    gpio_config(&io_conf);

    // Глобальная установка службы прерываний (если еще не была вызвана ранее в коде)
    ESP_ERROR_CHECK(gpio_install_isr_service(0)); 

    // Привязываем функцию ISR к конкретному пину
    gpio_isr_handler_add(PUMP_BUTTON_PIN, button_isr_handler, (void*) PUMP_BUTTON_PIN);
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
    pump_button_init(); // Инициализация кнопки с прерыванием

    // Ждем 5 секунд, чтобы Wi-Fi успел подключиться
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 1. Инициализация I2C
    init_i2c_master();
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

    init_pwm_converter(); // Инициализация ШИМ для конвертера

    // 3. Бесконечный цикл опроса
    int tick_count = 0;

    // Запускаем задачу экрана (выделяем 4096 байт памяти, средний приоритет)
    xTaskCreate(display_task, "oled_task", 4096, NULL, 5, NULL);
    
    // Запускаем задачу автоматического управления освещением (выделяем 4096 байт памяти, средний приоритет)
    xTaskCreate(auto_light, "auto_light_task", 4096, NULL, 5, NULL);

    while (1) {
        // 1. Опрашиваем BLYNK и датчики
        pump_state = read_blynk_pump_button();
        int button_slider = read_blynk_slider();
        read_gy21_data(&temp, &hum);
        adc_oneshot_read(adc1_handle, ADC_CHAN, &cms_raw);
        int water_percentage = get_water_percentage(get_stable_water_level());

        // 2. Включаем или выключаем физические пины
        gpio_set_level(PUMP_PIN, pump_state);
        set_converter_level(button_slider);

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
            send_to_blynk(temp, hum, cms_raw, water_percentage);
            tick_count = 0; // Сбрасываем счетчик
        }

        tick_count++;
        vTaskDelay(pdMS_TO_TICKS(5000)); // Базовая пауза цикла 5 секунды
    }
}