#include "lcc/lcc_traction.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_node.h"
#include "lcc/lcc_frame.h"
#include "lcc/lcc_if.h"

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

/* ---- Consist Config (REQ byte 0x30) ----------------------------------- */

static int consist_find(const lcc_train_state_t *t, uint64_t target)
{
    for (int i = 0; i < LCC_MAX_CONSIST_SLAVES; i++) {
        if (t->consist[i].node_id == target)
            return i;
    }
    return -1;
}

static int consist_count(const lcc_train_state_t *t)
{
    int n = 0;
    for (int i = 0; i < LCC_MAX_CONSIST_SLAVES; i++) {
        if (t->consist[i].node_id != 0)
            n++;
    }
    return n;
}

static int consist_find_nth(const lcc_train_state_t *t, int n)
{
    int seen = 0;
    for (int i = 0; i < LCC_MAX_CONSIST_SLAVES; i++) {
        if (t->consist[i].node_id == 0)
            continue;
        if (seen == n)
            return i;
        seen++;
    }
    return -1;
}

static void send_consist_attach_reply(const lcc_node_t *node, uint16_t reply_to,
                                      uint8_t sub_op, uint64_t target,
                                      uint16_t err)
{
    /* Attach/Detach reply: [0x30, sub, target_nid(6), err_hi, err_lo]. */
    uint8_t reply[10] = { LCC_TRAC_CONSIST_CFG, sub_op };
    lcc_node_id_to_buf(target, &reply[2]);
    reply[8] = (uint8_t)(err >> 8);
    reply[9] = (uint8_t)(err & 0xFF);
    lcc_node_send_addressed(node, LCC_MTI_TRACTION_CONTROL_REPLY,
                            reply_to, reply, sizeof(reply));
}

static void on_consist_attach(lcc_node_t *node, const uint8_t *payload,
                              uint8_t len, uint16_t reply_to)
{
    if (len < 9) return;  /* 0x30, sub, flags, 6-byte NID */
    uint64_t target = lcc_node_id_from_buf(&payload[3]);
    uint8_t  flags  = payload[2];

    /* Adding self as a consist slave is a no-op per OpenMRN. */
    if (target == node->id) {
        send_consist_attach_reply(node, reply_to, LCC_CNSTREQ_ATTACH_NODE,
                                  target, LCC_ERR_ALREADY_EXISTS);
        return;
    }

    lcc_train_state_t *t = node->train;
    int idx = consist_find(t, target);
    if (idx >= 0) {
        /* Already present: update flags only (OpenMRN semantics). */
        t->consist[idx].flags = flags;
        send_consist_attach_reply(node, reply_to, LCC_CNSTREQ_ATTACH_NODE,
                                  target, LCC_ERR_OK);
        return;
    }
    idx = consist_find(t, 0);
    if (idx < 0) {
        /* Table full — report a not-found-ish error. OpenMRN doesn't
         * define a specific code for "out of space"; ALREADY_EXISTS is
         * the closest non-OK consist-attach error in the spec. */
        send_consist_attach_reply(node, reply_to, LCC_CNSTREQ_ATTACH_NODE,
                                  target, LCC_ERR_ALREADY_EXISTS);
        return;
    }
    t->consist[idx].node_id = target;
    t->consist[idx].flags   = flags;
    send_consist_attach_reply(node, reply_to, LCC_CNSTREQ_ATTACH_NODE,
                              target, LCC_ERR_OK);
}

static void on_consist_detach(lcc_node_t *node, const uint8_t *payload,
                              uint8_t len, uint16_t reply_to)
{
    if (len < 9) return;
    uint64_t target = lcc_node_id_from_buf(&payload[3]);

    lcc_train_state_t *t = node->train;
    int idx = consist_find(t, target);
    uint16_t err = LCC_ERR_OK;
    if (idx < 0) {
        err = LCC_ERR_NOT_FOUND;
    } else {
        t->consist[idx].node_id = 0;
        t->consist[idx].flags   = 0;
    }
    send_consist_attach_reply(node, reply_to, LCC_CNSTREQ_DETACH_NODE,
                              target, err);
}

static void on_consist_query(lcc_node_t *node, const uint8_t *payload,
                             uint8_t len, uint16_t reply_to)
{
    lcc_train_state_t *t  = node->train;
    int                sz = consist_count(t);
    if (sz > 255) sz = 255;

    if (len > 2) {
        /* Long form: payload[2] is the index. Reply with full entry. */
        uint8_t id  = payload[2];
        int     pos = consist_find_nth(t, id);
        if (pos >= 0) {
            uint8_t reply[11] = {
                LCC_TRAC_CONSIST_CFG, LCC_CNSTREQ_QUERY_NODES,
                (uint8_t)sz, id, t->consist[pos].flags,
            };
            lcc_node_id_to_buf(t->consist[pos].node_id, &reply[5]);
            lcc_node_send_addressed(node, LCC_MTI_TRACTION_CONTROL_REPLY,
                                    reply_to, reply, sizeof(reply));
            return;
        }
        /* Fall through to short form when the index is out of range. */
    }

    uint8_t reply[3] = {
        LCC_TRAC_CONSIST_CFG, LCC_CNSTREQ_QUERY_NODES, (uint8_t)sz,
    };
    lcc_node_send_addressed(node, LCC_MTI_TRACTION_CONTROL_REPLY,
                            reply_to, reply, sizeof(reply));
}

static void on_consist_config(lcc_node_t *node, const uint8_t *payload,
                              uint8_t len, uint16_t reply_to)
{
    if (len < 2) return;
    switch (payload[1]) {
    case LCC_CNSTREQ_ATTACH_NODE: on_consist_attach(node, payload, len, reply_to); break;
    case LCC_CNSTREQ_DETACH_NODE: on_consist_detach(node, payload, len, reply_to); break;
    case LCC_CNSTREQ_QUERY_NODES: on_consist_query(node, payload, len, reply_to); break;
    default:
        break;
    }
}

/* ---- Consist forwarding ------------------------------------------------ */

/* Send a forwarded traction command to one consist slave. Sets the
 * REQ_LISTENER bit on payload[0] so the slave applies the command but
 * does not re-forward it to its own consist. Local slaves also get a
 * direct hand-off to the traction handler so the frame isn't reliant
 * on USB CDC loopback (which we don't have). */
static void forward_to_slave(lcc_node_t *src, lcc_node_t *dst,
                             uint8_t cmd, uint8_t flags,
                             const uint8_t *payload, uint8_t payload_len)
{
    uint8_t fwd[8];
    if (payload_len > sizeof(fwd)) return;
    fwd[0] = (uint8_t)(cmd | LCC_TRAC_REQ_LISTENER);
    if (payload_len > 1)
        memcpy(&fwd[1], &payload[1], (size_t)(payload_len - 1));

    /* SET_SPEED direction bit (payload[1] MSB) flips for CNSTFLAGS_REVERSE. */
    if ((cmd & 0x7F) == LCC_TRAC_SET_SPEED_DIR
        && (flags & LCC_CNSTFLAGS_REVERSE)
        && payload_len >= 3) {
        fwd[1] ^= 0x80;
    }

    lcc_node_send_addressed(src, LCC_MTI_TRACTION_CONTROL_COMMAND,
                            dst->alias, fwd, payload_len);

    /* Local loopback: synthesize the corresponding single-frame addressed
     * CAN frame and feed it to the slave's traction handler directly. */
    lcc_frame_t f;
    f.id = lcc_can_id(LCC_MTI_TRACTION_CONTROL_COMMAND, src->alias);
    lcc_frame_build_addressed_hdr(f.data, dst->alias, 0);
    if (payload_len > 0)
        memcpy(&f.data[2], fwd, payload_len);
    f.dlc = (uint8_t)(2 + payload_len);
    lcc_traction_handle_frame(dst, &f);
}

/* Decide whether a consist slave should receive this forwarded command.
 * SET_FN forwarding is gated by CNSTFLAGS_LINKF0 / LINKFN; other
 * commands always forward. */
static bool consist_should_forward(uint8_t cmd, uint8_t flags,
                                   const uint8_t *payload, uint8_t payload_len)
{
    if ((cmd & 0x7F) == LCC_TRAC_SET_FN && payload_len >= 4) {
        uint32_t addr = ((uint32_t)payload[1] << 16)
                      | ((uint32_t)payload[2] <<  8)
                      |  (uint32_t)payload[3];
        if (addr == 0)
            return (flags & LCC_CNSTFLAGS_LINKF0) != 0;
        return (flags & LCC_CNSTFLAGS_LINKFN) != 0;
    }
    return true;
}

static void forward_to_consist(lcc_node_t *src, uint16_t src_alias,
                               uint8_t cmd,
                               const uint8_t *payload, uint8_t payload_len)
{
    /* Forwarded commands carry REQ_LISTENER — never re-forward. */
    if (cmd & LCC_TRAC_REQ_LISTENER)
        return;

    lcc_train_state_t *t = src->train;
    for (int i = 0; i < LCC_MAX_CONSIST_SLAVES; i++) {
        uint64_t slave_id = t->consist[i].node_id;
        if (slave_id == 0)
            continue;
        uint8_t flags = t->consist[i].flags;

        if (!consist_should_forward(cmd, flags, payload, payload_len))
            continue;

        /* Skip if the original sender is the slave — avoids bouncing the
         * command back to the throttle when the throttle itself is in the
         * consist (rare but legal). */
        lcc_node_t *dst = NULL;
        for (int j = 0; j < lcc_node_count(); j++) {
            lcc_node_t *n = lcc_node_at(j);
            if (n && n->id == slave_id) { dst = n; break; }
        }
        if (!dst || dst == src)
            continue;
        if (dst->alias == src_alias)
            continue;

        forward_to_slave(src, dst, cmd, flags, payload, payload_len);
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
    uint8_t cmd = payload[0];
    uint8_t sub = (uint8_t)(cmd & 0x7F);
    switch (sub) {
    case LCC_TRAC_SET_SPEED_DIR:
        on_set_speed(node, payload, len);
        forward_to_consist(node, reply_to, cmd, payload, len);
        break;
    case LCC_TRAC_SET_FN:
        on_set_fn(node, payload, len);
        forward_to_consist(node, reply_to, cmd, payload, len);
        break;
    case LCC_TRAC_EMERGENCY_STOP:
        on_estop(node);
        forward_to_consist(node, reply_to, cmd, payload, len);
        break;
    case LCC_TRAC_QUERY_SPEEDS:   on_query_speeds(node, reply_to); break;
    case LCC_TRAC_QUERY_FUNCTION: on_query_function(node, payload, len, reply_to); break;
    case LCC_TRAC_CONTROLLER_CFG: on_controller_config(node, payload, len, reply_to); break;
    case LCC_TRAC_CONSIST_CFG:    on_consist_config(node, payload, len, reply_to); break;
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
