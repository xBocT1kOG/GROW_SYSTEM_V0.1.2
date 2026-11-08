#pragma once
#include <stddef.h>
#include <stdint.h>
uint32_t uxTaskGetStackHighWaterMark(void *task);
typedef void *TaskHandle_t;
int xTaskCreate(void (*function)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, TaskHandle_t *handle);
