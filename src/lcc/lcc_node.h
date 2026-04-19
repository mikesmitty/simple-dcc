#ifndef LCC_NODE_H
#define LCC_NODE_H

#include <stdint.h>
#include <stdbool.h>
#include "lcc/lcc_frame.h"

/*
 * Per-node state: identity, alias allocation progress, protocol
 * advertisements, and datagram reassembly buffer. One `lcc_node_t` per
 * OpenLCB node riding the shared GridConnect interface. M3 registers
 * only the command-station node; M5+ adds per-train nodes.
 */

typedef enum {
    LCC_ROLE_CS,
    LCC_ROLE_TRAIN,
} lcc_node_role_t;

typedef enum {
    LCC_A_IDLE,       /* about to begin alias allocation */
    LCC_A_CID,        /* CID frames emitted; ready to enter WAIT next tick */
    LCC_A_WAIT,       /* 200 ms silent-wait before claiming */
    LCC_A_CONFIRMED,  /* alias owned — node operational */
    LCC_A_CONFLICT,   /* collision seen — step LFSR + retry on next tick */
} lcc_alias_state_t;

typedef struct {
    uint16_t src_alias;
    uint8_t  buf[72];
    uint8_t  len;
    bool     active;
} lcc_datagram_rx_t;

struct lcc_snip;  /* defined in lcc_snip.h */

typedef struct lcc_node {
    uint64_t                id;
    uint16_t                alias;
    lcc_node_role_t         role;
    lcc_alias_state_t       alias_state;
    uint8_t                 alias_wait_ticks;
    uint8_t                 alias_retries;
    const struct lcc_snip  *snip;
    uint64_t                pip_bits;   /* PIP advertisement, lcc_defs.h LCC_PIP_* */
    lcc_datagram_rx_t       dg_rx;
} lcc_node_t;

/* Registry: fixed-size static array. CS + up to (LCC_MAX_NODES - 1) trains. */
#ifndef LCC_MAX_NODES
#define LCC_MAX_NODES  24
#endif

/* Zero the registry. Called once from lcc_task_init. */
void lcc_node_registry_reset(void);

/* Insert a node. Returns false if the registry is full. */
bool lcc_node_register(lcc_node_t *node);

/* Number of registered nodes. */
int lcc_node_count(void);

/* Lookup by index (0..count-1). Returns NULL on out-of-range. */
lcc_node_t *lcc_node_at(int idx);

/* Find a registered node by confirmed alias. Unconfirmed nodes are
 * invisible to this lookup so frames addressed to in-flight aliases
 * don't get mis-routed. */
lcc_node_t *lcc_node_find_by_alias(uint16_t alias);

/* Initialize node fields. Caller sets snip/pip_bits/role afterward. */
void lcc_node_init(lcc_node_t *node, uint64_t id, lcc_node_role_t role);

/* Per-node frame delivery: the dispatcher in lcc_if chooses which nodes
 * receive which frames; this routine runs the actual handlers. */
void lcc_node_handle_frame(lcc_node_t *node, const lcc_frame_t *frame);

/* TX helpers: each builds a frame with this node's alias and hands it
 * to lcc_if_tx. Addressed sends auto-fragment long payloads. */
void lcc_node_send_global(const lcc_node_t *node, uint16_t mti,
                          const uint8_t *data, uint8_t dlc);
void lcc_node_send_addressed(const lcc_node_t *node, uint16_t mti,
                             uint16_t dst_alias,
                             const uint8_t *payload, uint16_t payload_len);
void lcc_node_send_control(const lcc_node_t *node, uint16_t field,
                           const uint8_t *data, uint8_t dlc);

#endif /* LCC_NODE_H */
