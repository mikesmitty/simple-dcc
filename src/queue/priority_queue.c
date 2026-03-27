#include "queue/priority_queue.h"
#include <string.h>

void pqueue_init(priority_queue_t *pq, QueueHandle_t input, QueueHandle_t output) {
    pq->count = 0;
    pq->input_queue = input;
    pq->output_queue = output;
    memset(pq->items, 0, sizeof(pq->items));
}

static void sift_up(priority_queue_t *pq, int idx) {
    while (idx > 0) {
        int parent = (idx - 1) / 2;
        if (pq->items[idx]->priority <= pq->items[parent]->priority) break;
        dcc_packet_t *tmp = pq->items[idx];
        pq->items[idx] = pq->items[parent];
        pq->items[parent] = tmp;
        idx = parent;
    }
}

static void sift_down(priority_queue_t *pq, int idx) {
    for (;;) {
        int largest = idx;
        int left = 2 * idx + 1;
        int right = 2 * idx + 2;

        if (left < pq->count &&
            pq->items[left]->priority > pq->items[largest]->priority) {
            largest = left;
        }
        if (right < pq->count &&
            pq->items[right]->priority > pq->items[largest]->priority) {
            largest = right;
        }
        if (largest == idx) break;

        dcc_packet_t *tmp = pq->items[idx];
        pq->items[idx] = pq->items[largest];
        pq->items[largest] = tmp;
        idx = largest;
    }
}

static void remove_top(priority_queue_t *pq) {
    if (pq->count == 0) return;
    pq->count--;
    pq->items[0] = pq->items[pq->count];
    pq->items[pq->count] = NULL;
    if (pq->count > 0) {
        sift_down(pq, 0);
    }
}

void pqueue_push(priority_queue_t *pq, dcc_packet_t *pkt) {
    if (pq->count >= PQUEUE_CAPACITY) {
        packet_free(pkt);
        return;
    }
    pq->items[pq->count] = pkt;
    sift_up(pq, pq->count);
    pq->count++;
}

// Pop returns a packet the caller owns and must free.
// If the top packet has repeats, a copy is returned and the original
// stays in the queue with decremented repeats. Otherwise the original
// is removed and returned directly.
dcc_packet_t *pqueue_pop(priority_queue_t *pq) {
    if (pq->count == 0) return NULL;

    dcc_packet_t *top = pq->items[0];

    if (top->repeats > 0) {
        top->repeats--;
        // Allocate a copy for the caller
        dcc_packet_t *copy = packet_alloc();
        if (!copy) return NULL;
        memcpy(copy->data, top->data, top->len);
        copy->len = top->len;
        copy->address = top->address;
        copy->priority = top->priority;
        copy->repeats = 0;
        return copy;
    }

    // No repeats left — remove from heap and return original
    remove_top(pq);
    return top;
}

bool pqueue_contains(priority_queue_t *pq, dcc_packet_t *pkt) {
    for (int i = 0; i < pq->count; i++) {
        if (pq->items[i]->address == pkt->address &&
            pq->items[i]->len == pkt->len &&
            memcmp(pq->items[i]->data, pkt->data, pkt->len) == 0) {
            return true;
        }
    }
    return false;
}

bool pqueue_is_empty(priority_queue_t *pq) {
    return pq->count == 0;
}

void task_priority_queue(void *params) {
    priority_queue_t *pq = (priority_queue_t *)params;
    dcc_packet_t *pkt;

    for (;;) {
        // Drain input queue
        while (xQueueReceive(pq->input_queue, &pkt, 0) == pdTRUE) {
            if (!pqueue_contains(pq, pkt)) {
                pqueue_push(pq, pkt);
            } else {
                packet_free(pkt);
            }
        }

        // Dispatch highest priority packet to wavegen
        if (!pqueue_is_empty(pq)) {
            dcc_packet_t *out = pqueue_pop(pq);
            if (out) {
                if (xQueueSend(pq->output_queue, &out, pdMS_TO_TICKS(10)) != pdTRUE) {
                    packet_free(out);
                }
            }
        }

        vTaskDelay(1);
    }
}
