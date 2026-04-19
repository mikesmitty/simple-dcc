#include "lcc/lcc_traction.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_node.h"
#include "lcc/lcc_frame.h"

#include <string.h>

static lcc_traction_hooks_t g_hooks;

void lcc_traction_set_hooks(const lcc_traction_hooks_t *hooks)
{
    if (hooks)
        g_hooks = *hooks;
    else
        memset(&g_hooks, 0, sizeof(g_hooks));
}

void lcc_train_state_init(lcc_train_state_t *state,
                          uint16_t dcc_address,
                          bool is_long_address)
{
    memset(state, 0, sizeof(*state));
    state->dcc_address     = dcc_address;
    state->is_long_address = is_long_address;
}

/* OpenLCB float16 velocity → DCC 128-step. The 126/126.8 divisor matches the
 * formula JMRI's integration was tuned against; keep verbatim. */
static uint8_t f16_to_dcc_speed_step(lcc_float16_t v)
{
    if (lcc_float16_is_nan(v))
        return 1;  /* estop */
    if (lcc_float16_is_zero(v))
        return 0;  /* stop */

    float speed = lcc_float16_to_float(v);
    if (speed < 0.0f) speed = -speed;
    int step = (int)(speed * 126.0f / 126.8f) + 2;
    if (step < 2)   step = 2;
    if (step > 127) step = 127;
    return (uint8_t)step;
}

/* ---- per-sub-instruction handlers ------------------------------------- */

static void on_set_speed(lcc_node_t *node, const uint8_t *payload, uint8_t len)
{
    if (len < 3) return;
    lcc_float16_t v = (lcc_float16_t)((payload[1] << 8) | payload[2]);
    node->train->last_speed_f16 = v;
    node->train->emergency      = false;

    uint8_t  step    = f16_to_dcc_speed_step(v);
    bool     forward = !lcc_float16_is_negative(v);

    if (g_hooks.set_throttle)
        g_hooks.set_throttle(node->train->dcc_address, step, forward);
}

static void on_set_fn(lcc_node_t *node, const uint8_t *payload, uint8_t len)
{
    if (len < 6) return;
    uint32_t fn  = ((uint32_t)payload[1] << 16)
                 | ((uint32_t)payload[2] <<  8)
                 |  (uint32_t)payload[3];
    uint16_t val = (uint16_t)((payload[4] << 8) | payload[5]);
    bool     on  = (val != 0);

    if (fn > 68) return;  /* DCC engine caps at F68 (RCN-212 group 10) */

    if (fn < 64) {
        if (on) node->train->fn_state |=  (1ULL << fn);
        else    node->train->fn_state &= ~(1ULL << fn);
    } else {
        uint8_t bit = (uint8_t)(1u << (fn - 64));
        if (on) node->train->fn_state_hi |=  bit;
        else    node->train->fn_state_hi &= (uint8_t)~bit;
    }

    if (g_hooks.set_function)
        g_hooks.set_function(node->train->dcc_address, (uint16_t)fn, on);
}

static void on_estop(lcc_node_t *node)
{
    node->train->emergency = true;
    if (g_hooks.emergency_stop)
        g_hooks.emergency_stop(node->train->dcc_address);
}

static void on_query_speeds(lcc_node_t *node, uint16_t reply_to)
{
    /* Reply payload: 0x10 | set_f16 | status | commanded_f16 | actual_f16.
     * No feedback path yet — echo the last setpoint for all three speeds.
     * Status bit 0x80 = estop in effect. */
    lcc_float16_t v = node->train->last_speed_f16;
    uint8_t status  = node->train->emergency ? 0x80 : 0x00;
    uint8_t reply[8] = {
        LCC_TRAC_QUERY_SPEEDS,
        (uint8_t)(v >> 8), (uint8_t)v,
        status,
        (uint8_t)(v >> 8), (uint8_t)v,
        (uint8_t)(v >> 8), (uint8_t)v,
    };
    lcc_node_send_addressed(node, LCC_MTI_TRACTION_CONTROL_REPLY,
                            reply_to, reply, sizeof(reply));
}

static void on_query_function(lcc_node_t *node, const uint8_t *payload,
                              uint8_t len, uint16_t reply_to)
{
    if (len < 4) return;
    uint32_t fn = ((uint32_t)payload[1] << 16)
                | ((uint32_t)payload[2] <<  8)
                |  (uint32_t)payload[3];

    uint16_t val = 0;
    if (fn < 64)
        val = (node->train->fn_state >> fn) & 1ULL ? 1 : 0;
    else if (fn <= 68)
        val = (node->train->fn_state_hi >> (fn - 64)) & 1u ? 1 : 0;

    uint8_t reply[6] = {
        LCC_TRAC_QUERY_FUNCTION,
        payload[1], payload[2], payload[3],
        (uint8_t)(val >> 8), (uint8_t)val,
    };
    lcc_node_send_addressed(node, LCC_MTI_TRACTION_CONTROL_REPLY,
                            reply_to, reply, sizeof(reply));
}

/* ---- entry point ------------------------------------------------------- */

void lcc_traction_handle_frame(lcc_node_t *node, const lcc_frame_t *frame)
{
    if (!node || !node->train) return;
    if (frame->dlc < 3) return;  /* 2-byte header + at least 1 sub-cmd byte */

    /* M5 doesn't reassemble multi-frame addressed traction commands; all
     * supported sub-instructions fit in one CAN frame. */
    if (!lcc_frame_addressed_is_first(frame) ||
        !lcc_frame_addressed_is_last(frame))
        return;

    uint16_t       reply_to = lcc_get_src(frame->id);
    const uint8_t *payload  = &frame->data[2];
    uint8_t        len      = (uint8_t)(frame->dlc - 2);
    uint8_t        sub      = (uint8_t)(payload[0] & 0x7F);

    switch (sub) {
    case LCC_TRAC_SET_SPEED_DIR:  on_set_speed(node, payload, len); break;
    case LCC_TRAC_SET_FN:         on_set_fn(node, payload, len); break;
    case LCC_TRAC_EMERGENCY_STOP: on_estop(node); break;
    case LCC_TRAC_QUERY_SPEEDS:   on_query_speeds(node, reply_to); break;
    case LCC_TRAC_QUERY_FUNCTION: on_query_function(node, payload, len, reply_to); break;
    default:
        break;
    }
}
