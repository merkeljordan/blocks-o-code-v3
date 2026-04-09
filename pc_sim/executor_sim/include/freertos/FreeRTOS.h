#pragma once
#include <stdint.h>
#include <stdbool.h>

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xFFFFFFFF
#define pdMS_TO_TICKS(x) (x)

typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef void (*TaskFunction_t)(void *);

QueueHandle_t xQueueCreate(uint32_t length, uint32_t itemSize);
BaseType_t xQueueSend(QueueHandle_t xQueue, const void * pvItemToQueue, TickType_t xTicksToWait);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait);
uint32_t uxQueueMessagesWaiting(QueueHandle_t xQueue);

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex, TickType_t xBlockTime);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex);

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode, const char * const pcName,
        const uint32_t usStackDepth, void * const pvParameters, uint32_t uxPriority,
        void * const pvCreatedTask, const int xCoreID);

void vTaskDelay(TickType_t xTicksToDelay);
