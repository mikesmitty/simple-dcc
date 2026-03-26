#ifndef LCC_INTERFACE_H
#define LCC_INTERFACE_H

#include <stdint.h>
#include "dcc/dcc.h"
#include "FreeRTOS.h"
#include "queue.h"

void lcc_interface_init(dcc_engine_t *dcc, QueueHandle_t pqueue_input);
void lcc_100ms_tick(void);

// FreeRTOS task: runs OpenLcb_run() in a loop
void task_protocol(void *params);

#endif // LCC_INTERFACE_H
