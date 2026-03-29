#ifndef LCC_INTERFACE_H
#define LCC_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "dcc/dcc.h"
#include "track/track.h"
#include "FreeRTOS.h"
#include "queue.h"

#include "openlcb/openlcb_types.h"

void lcc_interface_load_config(void);
void lcc_interface_init(dcc_engine_t *dcc, track_t *track, QueueHandle_t pqueue_input);
void lcc_100ms_tick(void);

// Returns the dynamic base used for auto-created train node IDs.
node_id_t lcc_interface_get_train_node_id_base(void);

// Check incoming CAN frame for Verify Node ID targeting a train address;
// if matched, queues auto-creation of the train node.
struct can_msg_struct;
void lcc_interface_on_rx_can_msg(struct can_msg_struct *msg);

// FreeRTOS task: runs OpenLcb_run() in a loop
void task_protocol(void *params);

// Returns true if auto-claim of DCC addresses is enabled in config memory.
bool lcc_interface_auto_claim_enabled(void);

// Returns true if RailCom cutout is enabled in config memory.
bool lcc_interface_railcom_enabled(void);

// Returns the track current limits from config memory.
uint16_t lcc_interface_main_limit_ma(void);
uint16_t lcc_interface_prog_limit_ma(void);

// Returns the pin assignments for the motor drivers.
void lcc_interface_get_pins_main(uint8_t *sig, uint8_t *pwr, uint8_t *brk, uint8_t *flt, uint8_t *adc);
void lcc_interface_get_pins_prog(uint8_t *sig, uint8_t *pwr, uint8_t *brk, uint8_t *flt, uint8_t *adc);

#endif // LCC_INTERFACE_H
