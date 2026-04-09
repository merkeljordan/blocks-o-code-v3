#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "esp_err.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

// Global tee log file -- output goes to both terminal and sim_log.txt
FILE *g_sim_log_file = NULL;

void sim_log_init(const char *path) {
    g_sim_log_file = fopen(path, "w");
}

void sim_log_close(void) {
    if (g_sim_log_file) { fclose(g_sim_log_file); g_sim_log_file = NULL; }
}

// Tee: write a formatted line to stdout AND the log file
void sim_log_write(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    if (g_sim_log_file) {
        va_start(args, fmt);
        vfprintf(g_sim_log_file, fmt, args);
        va_end(args);
        fflush(g_sim_log_file);
    }
}

const char* esp_err_to_name(esp_err_t code) {
    if (code == ESP_OK) return "ESP_OK";
    return "ESP_FAIL";
}

int64_t esp_timer_get_time(void) {
    return GetTickCount64() * 1000;
}

void vTaskDelay(TickType_t xTicksToDelay) {
    Sleep(xTicksToDelay);
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) {
    CRITICAL_SECTION* cs = malloc(sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(cs);
    return (SemaphoreHandle_t)cs;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex, TickType_t xBlockTime) {
    EnterCriticalSection((CRITICAL_SECTION*)xMutex);
    return pdTRUE;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex) {
    LeaveCriticalSection((CRITICAL_SECTION*)xMutex);
    return pdTRUE;
}

#define MAX_QUEUE 128
typedef struct {
    void* data[MAX_QUEUE];
    int head, tail, count;
    uint32_t itemSize;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
} MockQueue;

QueueHandle_t xQueueCreate(uint32_t length, uint32_t itemSize) {
    MockQueue* q = malloc(sizeof(MockQueue));
    q->head = 0; q->tail = 0; q->count = 0; q->itemSize = itemSize;
    InitializeCriticalSection(&q->cs);
    InitializeConditionVariable(&q->cv);
    return q;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void * pvItemToQueue, TickType_t xTicksToWait) {
    MockQueue* q = (MockQueue*)xQueue;
    EnterCriticalSection(&q->cs);
    void* p = malloc(q->itemSize);
    memcpy(p, pvItemToQueue, q->itemSize);
    q->data[q->tail] = p;
    q->tail = (q->tail + 1) % MAX_QUEUE;
    q->count++;
    WakeConditionVariable(&q->cv);
    LeaveCriticalSection(&q->cs);
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait) {
    MockQueue* q = (MockQueue*)xQueue;
    EnterCriticalSection(&q->cs);
    while (q->count == 0) {
        if (xTicksToWait == 0) {
            LeaveCriticalSection(&q->cs);
            return pdFALSE;
        }
        SleepConditionVariableCS(&q->cv, &q->cs, xTicksToWait == portMAX_DELAY ? INFINITE : xTicksToWait);
    }
    void* p = q->data[q->head];
    memcpy(pvBuffer, p, q->itemSize);
    free(p);
    q->head = (q->head + 1) % MAX_QUEUE;
    q->count--;
    LeaveCriticalSection(&q->cs);
    return pdTRUE;
}

uint32_t uxQueueMessagesWaiting(QueueHandle_t xQueue) {
    MockQueue* q = (MockQueue*)xQueue;
    EnterCriticalSection(&q->cs);
    uint32_t cnt = q->count;
    LeaveCriticalSection(&q->cs);
    return cnt;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode, const char * const pcName,
        const uint32_t usStackDepth, void * const pvParameters, uint32_t uxPriority,
        void * const pvCreatedTask, const int xCoreID) {
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)pxTaskCode, pvParameters, 0, NULL);
    return pdPASS;
}
