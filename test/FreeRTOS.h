#ifndef FREERTOS_H
#define FREERTOS_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef void* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef void* TimerHandle_t;
typedef void* TaskHandle_t;
typedef uint32_t TickType_t;

#define portMAX_DELAY 0xFFFF
#define pdTRUE true
#define pdFALSE false
#define pdMS_TO_TICKS(x) (x)
#define portTICK_PERIOD_MS 1

static inline void xSemaphoreTake(SemaphoreHandle_t x, TickType_t t) {}
static inline void xSemaphoreGive(SemaphoreHandle_t x) {}
static inline void xTimerReset(TimerHandle_t x, TickType_t t) {}
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)1; }
static inline TimerHandle_t xTimerCreate(const char* name, TickType_t p, bool r, void* id, void* cb) { return (TimerHandle_t)1; }
static inline void xTimerStart(TimerHandle_t x, TickType_t t) {}
static inline void vTaskDelay(TickType_t t) {}

#endif
