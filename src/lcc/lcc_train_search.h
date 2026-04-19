#ifndef LCC_TRAIN_SEARCH_H
#define LCC_TRAIN_SEARCH_H

#include <stdint.h>
#include <stdbool.h>
#include "lcc/lcc_node.h"

/*
 * Train Search Protocol (OpenLCB S-9.7.0.2). Binds inbound EVENT_REPORT
 * frames whose event ID falls in the 0x090099FF........ space to the
 * train-node registry: every registered train checks if its DCC address
 * matches the encoded query and replies with PRODUCER_IDENTIFIED_VALID.
 *
 * If no train matches and the ALLOCATE flag is set, the allocator
 * callback (if registered) is asked to create a new train node. Because
 * fresh nodes can't reply until their alias confirms ~200 ms later, the
 * matching reply is deferred: the event ID is stashed on the node's
 * train state, and lcc_train_search_flush_pending (called from the
 * alias tick loop) emits the reply once the alias is live.
 */

/* Return true iff event_id is in the Train Search event space. */
bool lcc_train_search_is_event(uint64_t event_id);

/* Encode a DCC address + flags byte as a Train Search event ID.
 * Digits are decimal, right-justified, padded on the left with 0xF. */
uint64_t lcc_train_search_create_event(uint16_t address, uint8_t flags);

/* Extract the flags byte (low 8 bits) from a Train Search event ID. */
uint8_t lcc_train_search_flags(uint64_t event_id);

/* Decode the digit nibbles as a decimal DCC address (0xF padding skipped). */
uint16_t lcc_train_search_address(uint64_t event_id);

/* Allocator callback: create + register a new train node for the given
 * DCC address and stash event_id in node->train->pending_search_event
 * so the reply fires once the alias confirms. Return NULL on pool full. */
typedef lcc_node_t *(*lcc_train_alloc_fn)(uint16_t dcc_addr,
                                          uint8_t  flags,
                                          uint64_t event_id);

void lcc_train_search_set_allocator(lcc_train_alloc_fn alloc);

/* Entry point from lcc_if_dispatch. src_alias is the originating node's
 * alias; event_id is the full 64-bit event from the EVENT_REPORT payload. */
void lcc_train_search_dispatch(uint16_t src_alias, uint64_t event_id);

/* Auto-allocate a train node from a well-known DCC-proxy node ID
 * (06.01.00.00.XX.XX). JMRI's LCCPro throttle probes for trains by
 * sending Verify Node ID Global with the well-known ID before falling
 * back to the Train Search event — this hook creates the train up front
 * so the alias claim emits AMD and the node becomes visible. No-op if
 * node_id isn't in the proxy range or no allocator is registered. */
void lcc_train_search_ensure_by_node_id(uint64_t node_id);

/* Tick hook: emit deferred PRODUCER_IDENTIFIED_VALID for any train
 * whose alias has now confirmed. */
void lcc_train_search_flush_pending(void);

#endif /* LCC_TRAIN_SEARCH_H */
