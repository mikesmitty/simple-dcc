#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include "dcc/packet.h"
#include "FreeRTOS.h"
#include "queue.h"

#define PQUEUE_CAPACITY 64

typedef struct {
    dcc_packet_t *items[PQUEUE_CAPACITY];
    int           count;
    QueueHandle_t input_queue;  // receives packets from DCC engine / protocol
    QueueHandle_t output_queue; // sends to wavegen
} priority_queue_t;

void pqueue_init(priority_queue_t *pq, QueueHandle_t input, QueueHandle_t output);
void pqueue_push(priority_queue_t *pq, dcc_packet_t *pkt);
dcc_packet_t *pqueue_pop(priority_queue_t *pq);
bool pqueue_contains(priority_queue_t *pq, dcc_packet_t *pkt);
bool pqueue_is_empty(priority_queue_t *pq);

// FreeRTOS task
void task_priority_queue(void *params);

#endif // PRIORITY_QUEUE_H
