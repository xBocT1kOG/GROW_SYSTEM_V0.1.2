#pragma once
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
void vQueueDelete(QueueHandle_t queue);
int xQueueSend(QueueHandle_t queue, const void *event, TickType_t wait);
int xQueueReceive(QueueHandle_t queue, void *event, TickType_t wait);
