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
#include "serial/serial.h"
#include "protocol/lcc_interface.h"
#include "motor/motor.h"
#include "motor/profile.h"
#include "track/track.h"
#include "util/event_bus.h"

#define WAVEGEN_QUEUE_DEPTH  8
#define PQUEUE_INPUT_DEPTH  16

static wavegen_t         wavegen;
static dcc_engine_t      dcc_engine;
static priority_queue_t  pqueue;
static motor_t           motor_a;
static motor_t           motor_b;
static track_t           track_main;
static track_t           track_prog;
static event_bus_t       event_bus;

QueueHandle_t wavegen_queue;
static QueueHandle_t pqueue_input_queue;

// Track monitor task params
static motor_t *motors[] = { &motor_a, &motor_b };
typedef struct {
    motor_t **motors;
    uint8_t   count;
} track_monitor_params_t;
static track_monitor_params_t monitor_params = {
    .motors = motors,
    .count = 2,
};

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

    // Init wavegen (PIO)
    if (!wavegen_init(&wavegen, WAVEGEN_NORMAL,
                      PIN_SIGNAL_A, 2, PIN_BRAKE_A)) {
        printf("wavegen init failed\n");
        for (;;) {}
    }

    // Init packet scheduling
    pqueue_init(&pqueue, pqueue_input_queue, wavegen_queue);
    dcc_init(&dcc_engine, pqueue_input_queue);

    // Init motor drivers and tracks
    motor_init(&motor_a, 'A', PIN_POWER_A, PIN_SIGNAL_A, PIN_BRAKE_A,
               PIN_FAULT_A, ADC_CHANNEL_A, ADC_CURRENT_LIMIT_MAIN);
    motor_init(&motor_b, 'B', PIN_POWER_B, PIN_SIGNAL_B, PIN_BRAKE_B,
               PIN_FAULT_B, ADC_CHANNEL_B, ADC_CURRENT_LIMIT_MAIN);
    track_init(&track_main, 'A', TRACK_MODE_MAIN, &motor_a);
    track_init(&track_prog, 'B', TRACK_MODE_PROG, &motor_b);

    // Init event bus and LCC
    event_bus_init(&event_bus);
    lcc_interface_init(&dcc_engine, pqueue_input_queue);

    // Create tasks (highest priority first)
    xTaskCreate(task_wavegen,        "wavegen",   512,  &wavegen,         6, NULL);
    xTaskCreate(task_priority_queue, "pqueue",    512,  &pqueue,          5, NULL);
    xTaskCreate(task_track_monitor,  "trackmon",  512,  &monitor_params,  4, NULL);
    xTaskCreate(task_dcc_reminder,   "reminder",  512,  &dcc_engine,      3, NULL);
    xTaskCreate(task_protocol,       "protocol",  1024, NULL,             2, NULL);
    xTaskCreate(task_serial,         "serial",    1024, NULL,             1, NULL);
    xTaskCreate(task_blink,          "blink",     configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    printf("simple-dcc starting\n");
    vTaskStartScheduler();

    for (;;) {}
}
