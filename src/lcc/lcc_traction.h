#ifndef LCC_TRACTION_H
#define LCC_TRACTION_H

#include <stdint.h>
#include <stdbool.h>
#include "lcc/lcc_float16.h"
#include "lcc/lcc_node.h"
#include "lcc/lcc_frame.h"

/*
 * Traction protocol handler. Routes addressed MTI 0x05EB sub-instructions
 * (SET_SPEED_DIRECTION, SET_FN, EMERGENCY_STOP, QUERY_SPEEDS, QUERY_FUNCTION)
 * onto a hooks table the glue layer fills in with dcc_*() callbacks. Per-train
 * state hangs off lcc_node_t.train.
 *
 * M5 scope: the five commands above. Multi-frame addressed payloads are
 * dropped (single-frame is enough for everything M5 handles). Controller /
 * Listener / Consist sub-instructions land in M6 / M8.
 */

typedef struct lcc_train_state {
    uint16_t      dcc_address;
    bool          is_long_address;
    lcc_float16_t last_speed_f16;   /* echoed back by QUERY_SPEEDS */
    uint64_t      fn_state;         /* F0..F63 bitmap */
    uint8_t       fn_state_hi;      /* F64..F68 in bits 0..4 */
    bool          emergency;
} lcc_train_state_t;

typedef struct {
    void (*set_throttle)(uint16_t addr, uint8_t step, bool forward);
    void (*set_function)(uint16_t addr, uint16_t fn, bool on);
    void (*emergency_stop)(uint16_t addr);
} lcc_traction_hooks_t;

/* Wire DCC engine callbacks. Pass NULL to detach (e.g. for host tests). */
void lcc_traction_set_hooks(const lcc_traction_hooks_t *hooks);

/* Initialize a train state struct in place. */
void lcc_train_state_init(lcc_train_state_t *state,
                          uint16_t dcc_address,
                          bool is_long_address);

/* Handle a TRACTION_CONTROL_COMMAND frame whose addressed-destination has
 * already been verified to be `node`. Caller has not stripped the addressed
 * header. Frames without `node->train` set are silently dropped. */
void lcc_traction_handle_frame(lcc_node_t *node, const lcc_frame_t *frame);

#endif /* LCC_TRACTION_H */
