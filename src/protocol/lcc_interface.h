#ifndef LCC_INTERFACE_H
#define LCC_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "dcc/dcc.h"
#include "track/track.h"
#include "FreeRTOS.h"
#include "queue.h"

void lcc_interface_init(dcc_engine_t *dcc, track_t *track, QueueHandle_t pqueue_input);
void lcc_100ms_tick(void);

// Check incoming CAN frame for Verify Node ID targeting a train address;
// if matched, queues auto-creation of the train node.
struct can_msg_struct;
void lcc_interface_on_rx_can_msg(struct can_msg_struct *msg);

// FreeRTOS task: runs OpenLcb_run() in a loop
void task_protocol(void *params);

// Returns true if auto-claim of DCC addresses is enabled in config memory.
bool lcc_interface_auto_claim_enabled(void);

#endif // LCC_INTERFACE_H
