#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board_config.h"
#include "dcc/packet.h"
#include "dcc/dcc.h"
#include "wavegen/wavegen.h"
#include "queue/priority_queue.h"

#define WAVEGEN_QUEUE_DEPTH  8
#define PQUEUE_INPUT_DEPTH  16

static wavegen_t         wavegen;
static dcc_engine_t      dcc_engine;
static priority_queue_t  pqueue;

QueueHandle_t wavegen_queue;
static QueueHandle_t pqueue_input_queue;

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task;
    printf("Stack overflow in task: %s\n", name);
    for (;;) {}
}

static void task_blink(void *params) {
    (void)params;
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);

    for (;;) {
        gpio_put(PIN_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_put(PIN_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void) {
    stdio_init_all();
    packet_pool_init();

    // Create inter-task queues
    wavegen_queue = xQueueCreate(WAVEGEN_QUEUE_DEPTH, sizeof(dcc_packet_t *));
    pqueue_input_queue = xQueueCreate(PQUEUE_INPUT_DEPTH, sizeof(dcc_packet_t *));

    // Init subsystems
    if (!wavegen_init(&wavegen, WAVEGEN_NORMAL,
                      PIN_SIGNAL_A, 2, PIN_BRAKE_A)) {
        printf("wavegen init failed\n");
        for (;;) {}
    }

    pqueue_init(&pqueue, pqueue_input_queue, wavegen_queue);
    dcc_init(&dcc_engine, pqueue_input_queue);

    // Create tasks (highest priority first)
    xTaskCreate(task_wavegen,        "wavegen",  512, &wavegen,    6, NULL);
    xTaskCreate(task_priority_queue, "pqueue",   512, &pqueue,     5, NULL);
    xTaskCreate(task_dcc_reminder,   "reminder", 512, &dcc_engine, 3, NULL);
    xTaskCreate(task_blink,          "blink",    configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    printf("simple-dcc starting\n");
    vTaskStartScheduler();

    for (;;) {}
}
