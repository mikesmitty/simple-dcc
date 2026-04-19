#ifndef LCC_ALIAS_H
#define LCC_ALIAS_H

#include <stdint.h>
#include "lcc/lcc_node.h"

/*
 * 12-bit alias allocator. Each node runs its own state machine; the
 * allocator below is driven from the 100 ms tick in lcc_task and
 * serializes CID-in-flight across nodes via lcc_if's alias_mtx.
 */

/* Seed a starting alias candidate from a 48-bit node ID (OpenMRN
 * AliasAllocator: xor the four 12-bit chunks of the node ID). */
uint16_t lcc_alias_seed(uint64_t node_id);

/* Step the 12-bit LFSR. The polynomial matches OpenMRN so aliases
 * cycle through the same sequence implementations converge on. */
uint16_t lcc_alias_next(uint16_t alias);

/* Advance the alias state machine by one tick (~100 ms). Call for every
 * registered node. Transitions: IDLE → CID → WAIT → CONFIRMED, with
 * CONFLICT branching back to IDLE under a fresh LFSR step. */
void lcc_alias_tick(lcc_node_t *node);

/* Called from the RX dispatcher when a frame arrives that collides
 * with this node's in-flight or confirmed alias. The node transitions
 * to CONFLICT (if still allocating) or emits a defensive RID (if
 * CONFIRMED). */
void lcc_alias_on_collision(lcc_node_t *node);

/* Re-emit RID + AMD + INITIALIZATION_COMPLETE for a CONFIRMED node.
 * Used after the USB CDC link comes up mid-session so the host sees our
 * node-alias mapping without having to AME-probe. No-op if the node
 * hasn't reached CONFIRMED yet (the tick loop will emit naturally). */
void lcc_alias_reannounce(const lcc_node_t *node);

#endif /* LCC_ALIAS_H */
