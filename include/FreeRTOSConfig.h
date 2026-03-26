#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

// ---- Scheduling ----
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configNUMBER_OF_CORES                   1   // Single-core for now; set to 2 for SMP
#define configRUN_MULTIPLE_PRIORITIES           0
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                256  // words
#define configIDLE_SHOULD_YIELD                 1

// ---- ARM Cortex-M33 (RP2350) port settings ----
#define configENABLE_FPU                        1
#define configENABLE_MPU                        0
#define configENABLE_TRUSTZONE                  0
#define configUSE_16_BIT_TICKS                  0

// Cortex-M33 interrupt priority configuration
// RP2350 has 4 priority bits (0-15). FreeRTOS needs the raw shifted value.
#define configPRIO_BITS                         4
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configKERNEL_INTERRUPT_PRIORITY         (15 << (8 - configPRIO_BITS))

// ---- Memory ----
#define configTOTAL_HEAP_SIZE                   (32 * 1024)
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSTACK_DEPTH_TYPE                  uint32_t

// ---- Features ----
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_QUEUE_SETS                    0
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            512

// ---- Hook functions ----
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configCHECK_FOR_STACK_OVERFLOW          2  // Enable stack overflow checking

// ---- Assertions ----
#define configASSERT(x) do { if (!(x)) { __asm volatile("bkpt #0"); for (;;); } } while (0)

// ---- Debug/stats ----
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           0

// ---- API includes ----
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_xSemaphoreGetMutexHolder        1
#define INCLUDE_xTimerPendFunctionCall          1

#endif // FREERTOS_CONFIG_H
