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
#include "tasks/display_task.h"
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

#include "config/board_config.h"
#include "config/app_config.h"
#include "app/app_control.h"
#include "drivers/pump.h"
#include "drivers/light_pwm.h"
#include "tasks/input_task.h"
#include "tasks/time_task.h"
#include "tasks/ramp_task.h"
#include "services/time_service.h"
#include "drivers/htu21.h"
#include "drivers/soil.h"
#include "drivers/ldr.h"
#include "drivers/ultrasonic.h"
#include "tasks/sensor_task.h"
#include "tasks/network_task.h"

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
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));

    // HTU21 device registration belongs to htu21_init(); OLED stays legacy.
    i2c_device_config_t oled_cfg = { 
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, 
        .device_address = OLED_I2C_ADDRESS,
        .scl_speed_hz = I2C_CLOCK_HZ
    };
    const esp_err_t oled_err = i2c_master_bus_add_device(bus_handle, &oled_cfg, &oled_handle);
    if (oled_err != ESP_OK) {
        ESP_LOGE(TAG, "OLED device setup failed: %s", esp_err_to_name(oled_err));
    }
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

    // Установка локального часового пояса с автоматическим переходом на летнее/зимнее время
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();
    time_service_init(); // Register sync validity after configuring local time.
    esp_sntp_init();
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
    gpio_reset_pin(ULTRASONIC_TRIG_PIN);
    gpio_set_direction(ULTRASONIC_TRIG_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(ULTRASONIC_TRIG_PIN, 0); // По умолчанию выключен

    // Настраиваем ECHO как вход
    gpio_reset_pin(ULTRASONIC_ECHO_PIN);
    gpio_set_direction(ULTRASONIC_ECHO_PIN, GPIO_MODE_INPUT);
}

// ---------------------------------------------------------
// ФУНКЦИЯ ИЗМЕРЕНИЯ ДИСТАНЦИИ УЛЬТРАЗВУКОМ
// ---------------------------------------------------------
float get_water_distance_cm(void) {
    // 1. Посылаем стартовый импульс длиной 10 микросекунд
    gpio_set_level(ULTRASONIC_TRIG_PIN, 1);
    ets_delay_us(10);
    gpio_set_level(ULTRASONIC_TRIG_PIN, 0);

    // 2. Ждем, пока датчик поднимет ECHO в HIGH (начало ответа)
    int64_t start_wait = esp_timer_get_time();
    while (gpio_get_level(ULTRASONIC_ECHO_PIN) == 0) {
        // Таймаут, чтобы код не завис, если датчик отключится
        if (esp_timer_get_time() - start_wait > 20000) {
            ESP_LOGE(TAG, "Таймаут датчика уровня: нет ответа");
            return -1.0; 
        }
    }
    int64_t echo_start = esp_timer_get_time(); // Фиксируем время старта

    // 3. Ждем, пока датчик опустит ECHO в LOW (конец ответа)
    while (gpio_get_level(ULTRASONIC_ECHO_PIN) == 1) {
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
    if (current_distance > WATER_DISTANCE_EMPTY_CM) {
        current_distance = WATER_DISTANCE_EMPTY_CM;
    }
    if (current_distance < WATER_DISTANCE_FULL_CM) {
        current_distance = WATER_DISTANCE_FULL_CM;
    }

    // 2. Сама математика (инвертированная, так как чем меньше см, тем больше воды)
    // (16 - текущая_дистанция) / (16 - 6) * 100
    float percentage = ((WATER_DISTANCE_EMPTY_CM - current_distance) / (WATER_DISTANCE_EMPTY_CM - WATER_DISTANCE_FULL_CM)) * 100.0f;

    // 3. Округляем до математически правильного целого числа и возвращаем
    return (int)(percentage + 0.5f);
}

// 1. Инициализация ШИМ (вызвать один раз внутри app_main)
void init_pwm_converter(void) {
    // Настройка таймера
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LIGHT_PWM_MODE,
        .timer_num        = LIGHT_PWM_TIMER,
        .duty_resolution  = LIGHT_PWM_RESOLUTION,
        .freq_hz          = LIGHT_PWM_FREQUENCY_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Настройка канала и привязка к пину
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LIGHT_PWM_MODE,
        .channel        = LIGHT_PWM_CHANNEL,
        .timer_sel      = LIGHT_PWM_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LIGHT_PWM_GPIO,
        .duty           = 0, // При старте выдаем 0 (0 Вольт на выходе)
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    
    ESP_LOGI("PWM", "ШИМ конвертер инициализирован на пине %d", LIGHT_PWM_GPIO);
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
    ESP_ERROR_CHECK(ledc_set_duty(LIGHT_PWM_MODE, LIGHT_PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LIGHT_PWM_MODE, LIGHT_PWM_CHANNEL));
    
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

// Legacy OLED task moved to tasks/display_task.c; see Git history for reference.

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

    // ---------------------------------------------------------
    // V0.2 LOCAL CONTROL STARTUP
    // ---------------------------------------------------------
    ESP_ERROR_CHECK(pump_init());
    ESP_ERROR_CHECK(light_pwm_init());
    ESP_ERROR_CHECK(app_control_start());
    ESP_ERROR_CHECK(input_task_start());

    // Инициализация ультразвукового датчика
    ESP_ERROR_CHECK(ultrasonic_init()); // Legacy ultrasonic startup disabled.

    ESP_LOGI(TAG, "=== Запуск тестирования оборудования ===");

    // Networking is optional; local control does not depend on its startup.
    const esp_err_t network_err = network_task_start(WIFI_SSID, WIFI_PASS, BLYNK_TOKEN);
    if (network_err != ESP_OK) {
        ESP_LOGE(TAG, "Network startup failed: %s", esp_err_to_name(network_err));
    }
    ESP_ERROR_CHECK(time_task_start());
    ESP_ERROR_CHECK(ramp_task_start());
    // Legacy button startup disabled; input_task owns all four buttons.

    // Ждем 5 секунд, чтобы Wi-Fi успел подключиться
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 1. Инициализация I2C
    init_i2c_master();
    ESP_LOGI(TAG, "Шина I2C инициализирована на пинах %d (SDA) и %d (SCL)", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    // 2. Инициализация ADC1 (Емкостной датчик)
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = SENSOR_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // Shared ADC1, one channel per driver; only sensor_task performs reads.
    ESP_ERROR_CHECK(soil_init(adc1_handle));
    ESP_ERROR_CHECK(ldr_init(adc1_handle));
    ESP_ERROR_CHECK(htu21_init(bus_handle));
    ESP_ERROR_CHECK(sensor_task_start());

    // Legacy PWM initialization disabled; light_pwm_init() ran once above.

    // Display startup is optional; control and sensors stay active on failure.
    const esp_err_t display_err = display_task_start();
    if (display_err != ESP_OK) {
        ESP_LOGE(TAG, "Display startup failed: %s", esp_err_to_name(display_err));
    }

    // Legacy Wi-Fi/Blynk implementations remain reference-only.
    vTaskDelete(NULL);
}
