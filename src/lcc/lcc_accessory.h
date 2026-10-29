#ifndef LCC_ACCESSORY_H
#define LCC_ACCESSORY_H

#include <stdint.h>
#include <stdbool.h>
#include "lcc/lcc_node.h"

/*
 * DCC accessory decoders through the well-known accessory events: the
 * basic accessory activate and deactivate ranges (turnouts) and the
 * extended accessory range (signal aspects). The node consumes all three
 * and turns each Event Report into an accessory packet through the hooks,
 * as OpenMRN's DccAccyConsumer and DccExtAccyConsumer do.
 *
 * Basic accessories remember each output's last state, so Identify
 * Consumer says Valid or Invalid once an event has set it. Extended
 * accessories keep no state, and their replies say Unknown.
 */

typedef struct {
    /* `addr` is the 11-bit binary output address (see LCC_EVENT_ACCY_*). */
    void (*basic)(uint16_t addr, bool normal, bool activate);
    void (*extended)(uint16_t addr, uint8_t aspect);
} lcc_accessory_hooks_t;

/* Forget every output's state and register the three ranges on `node`.
 * Call after lcc_events_reset. */
void lcc_accessory_init(lcc_node_t *node, const lcc_accessory_hooks_t *hooks);

#endif /* LCC_ACCESSORY_H */
