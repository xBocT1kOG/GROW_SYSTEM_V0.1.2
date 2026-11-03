#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "hal/adc_types.h"

// ---------------------------------------------------------
// BOARD CONNECTIONS
// ---------------------------------------------------------
#define I2C_MASTER_SDA_IO        GPIO_NUM_11 // Shared sensor/display bus
#define I2C_MASTER_SCL_IO        GPIO_NUM_12
#define I2C_MASTER_NUM           I2C_NUM_0
#define HTU21_I2C_ADDRESS        0x40
#define OLED_I2C_ADDRESS         0x3C
#define I2C_CLOCK_HZ             100000

#define SENSOR_ADC_UNIT         ADC_UNIT_1
#define SOIL_ADC_CHANNEL        ADC_CHANNEL_3 // GPIO4
#define LDR_ADC_CHANNEL         ADC_CHANNEL_5 // GPIO6

#define PUMP_BUTTON_PIN         GPIO_NUM_18
#define AUTOLIGHT_BUTTON_PIN    GPIO_NUM_35
#define LIGHT_PLUS_BUTTON_PIN   GPIO_NUM_36
#define LIGHT_MINUS_BUTTON_PIN  GPIO_NUM_37
#define PUMP_PIN                GPIO_NUM_13 // Active high
#define ULTRASONIC_TRIG_PIN     GPIO_NUM_14
#define ULTRASONIC_ECHO_PIN     GPIO_NUM_2

// ---------------------------------------------------------
// LIGHT PWM
// ---------------------------------------------------------
#define LIGHT_PWM_GPIO          GPIO_NUM_17
#define LIGHT_PWM_TIMER         LEDC_TIMER_0
#define LIGHT_PWM_MODE          LEDC_LOW_SPEED_MODE
#define LIGHT_PWM_CHANNEL       LEDC_CHANNEL_0
#define LIGHT_PWM_RESOLUTION    LEDC_TIMER_8_BIT // Duty range: 0..255
#define LIGHT_PWM_FREQUENCY_HZ  1000
