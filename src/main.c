#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board_config.h"
#include "dcc/packet.h"
#include "wavegen/wavegen.h"

#define WAVEGEN_QUEUE_DEPTH 8

static wavegen_t wavegen;
extern QueueHandle_t wavegen_queue;

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

    wavegen_queue = xQueueCreate(WAVEGEN_QUEUE_DEPTH, sizeof(dcc_packet_t *));

    if (!wavegen_init(&wavegen, WAVEGEN_NORMAL,
                      PIN_SIGNAL_A, 2, PIN_BRAKE_A)) {
        printf("wavegen init failed\n");
        for (;;) {}
    }

    xTaskCreate(task_wavegen, "wavegen", 512, &wavegen, 6, NULL);
    xTaskCreate(task_blink, "blink", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    printf("simple-dcc starting\n");
    vTaskStartScheduler();

    for (;;) {}
}
