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

/* ---- Controller Config (REQ byte 0x20) -------------------------------- */

/* The ASSIGN/RELEASE wire format packs an optional 2-byte controller alias
 * at payload[9..10], flagged by bit 0 of payload[2]. We don't use the alias
 * here — node-ID equality is what matters for owner tracking — but the
 * length check still has to accept the 11-byte variant. */
static bool parse_assign_payload(const uint8_t *payload, uint8_t len,
                                 uint64_t *out_node_id)
{
    if (len < 9) return false;            /* 0x20, sub, flags, 6-byte NID */
    *out_node_id = lcc_node_id_from_buf(&payload[3]);
    return true;
}

static void on_assign_controller(lcc_node_t *node, const uint8_t *payload,
                                 uint8_t len, uint16_t reply_to)
{
    uint8_t reply[3] = { LCC_TRAC_CONTROLLER_CFG, LCC_CTRLREQ_ASSIGN,
                         LCC_CTRLRESP_OK };

    uint64_t new_owner;
    if (!parse_assign_payload(payload, len, &new_owner)) {
        reply[2] = LCC_CTRLRESP_ERR_CONTROLLER;
        lcc_node_send_addressed(node, LCC_MTI_TRACTION_CONTROL_REPLY,
                                reply_to, reply, sizeof(reply));
        return;
    }

    /* "Steal" semantics: a second ASSIGN unconditionally overwrites the
     * owner. OpenMRN does the same (TractionTrain.cxx:319 — the notify-old-
     * owner branch is gated off). The spec wants a NOTIFY_CONTROLLER_CHANGED
     * round-trip with the previous owner; deferred until we have a real
     * use case that depends on it. */
    node->train->controller_owner = new_owner;
    lcc_node_send_addressed(node, LCC_MTI_TRACTION_CONTROL_REPLY,
                            reply_to, reply, sizeof(reply));
}

static void on_release_controller(lcc_node_t *node, const uint8_t *payload,
                                  uint8_t len)
{
    uint64_t released;
    if (!parse_assign_payload(payload, len, &released))
        return;

    /* Only clear if the caller actually owns us — a stale release from a
     * throttle that never assigned should not free the train. */
    if (node->train->controller_owner == released)
        node->train->controller_owner = 0;
}

static void on_query_controller(lcc_node_t *node, uint16_t reply_to)
{
    /* Reply: [0x20, 0x03, flags, NID(6)]. We don't cache the controller's
     * alias, so bit 0 of flags stays clear and the optional 2-byte alias
     * suffix is omitted. */
    uint8_t reply[9] = {
        LCC_TRAC_CONTROLLER_CFG,
        LCC_CTRLREQ_QUERY,
        0x00,
    };
    lcc_node_id_to_buf(node->train->controller_owner, &reply[3]);
    lcc_node_send_addressed(node, LCC_MTI_TRACTION_CONTROL_REPLY,
                            reply_to, reply, sizeof(reply));
}

static void on_controller_config(lcc_node_t *node, const uint8_t *payload,
                                 uint8_t len, uint16_t reply_to)
{
    if (len < 2) return;
    switch (payload[1]) {
    case LCC_CTRLREQ_ASSIGN:  on_assign_controller(node, payload, len, reply_to); break;
    case LCC_CTRLREQ_RELEASE: on_release_controller(node, payload, len); break;
    case LCC_CTRLREQ_QUERY:   on_query_controller(node, reply_to); break;
    default:
        break;
    }
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

static void dispatch_traction(lcc_node_t *node, uint16_t reply_to,
                              const uint8_t *payload, uint8_t len)
{
    if (len < 1) return;
    uint8_t sub = (uint8_t)(payload[0] & 0x7F);
    switch (sub) {
    case LCC_TRAC_SET_SPEED_DIR:  on_set_speed(node, payload, len); break;
    case LCC_TRAC_SET_FN:         on_set_fn(node, payload, len); break;
    case LCC_TRAC_EMERGENCY_STOP: on_estop(node); break;
    case LCC_TRAC_QUERY_SPEEDS:   on_query_speeds(node, reply_to); break;
    case LCC_TRAC_QUERY_FUNCTION: on_query_function(node, payload, len, reply_to); break;
    case LCC_TRAC_CONTROLLER_CFG: on_controller_config(node, payload, len, reply_to); break;
    default:
        break;
    }
}

void lcc_traction_handle_frame(lcc_node_t *node, const lcc_frame_t *frame)
{
    if (!node || !node->train) return;
    if (frame->dlc < 2) return;  /* must at least carry the addressed header */

    uint16_t       src      = lcc_get_src(frame->id);
    const uint8_t *payload  = &frame->data[2];
    uint8_t        len      = (uint8_t)(frame->dlc - 2);
    bool           is_first = lcc_frame_addressed_is_first(frame);
    bool           is_last  = lcc_frame_addressed_is_last(frame);
    lcc_train_state_t *t    = node->train;

    /* Single-frame fast path — avoid touching the reassembly buffer. */
    if (is_first && is_last) {
        dispatch_traction(node, src, payload, len);
        return;
    }

    if (is_first) {
        /* New first-frame: claim the reassembly slot. Any partial in
         * flight from another sender is discarded. */
        t->rx_active = true;
        t->rx_src    = src;
        t->rx_len    = 0;
        if (len > sizeof(t->rx_buf)) {
            t->rx_active = false;
            return;
        }
        memcpy(t->rx_buf, payload, len);
        t->rx_len = len;
        return;
    }

    /* Continuation / final frame: must match the active sender. */
    if (!t->rx_active || t->rx_src != src)
        return;

    if ((size_t)t->rx_len + len > sizeof(t->rx_buf)) {
        t->rx_active = false;
        return;
    }
    memcpy(&t->rx_buf[t->rx_len], payload, len);
    t->rx_len = (uint8_t)(t->rx_len + len);

    if (is_last) {
        dispatch_traction(node, src, t->rx_buf, t->rx_len);
        t->rx_active = false;
        t->rx_len    = 0;
    }
}
