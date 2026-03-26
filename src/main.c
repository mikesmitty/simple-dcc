#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "board_config.h"

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

    xTaskCreate(task_blink, "blink", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    printf("simple-dcc starting\n");
    vTaskStartScheduler();

    // Should never reach here
    for (;;) {}
}
