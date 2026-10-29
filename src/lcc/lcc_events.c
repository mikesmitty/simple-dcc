#include "lcc/lcc_events.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_if.h"

#include <string.h>

typedef struct {
    lcc_node_t            *node;
    uint64_t               event_id;
    uint8_t                flags;
    lcc_event_on_report_fn on_report;
    lcc_event_state_fn     state;
    bool                   used;
} lcc_event_reg_t;

typedef struct {
    lcc_node_t            *node;
    uint64_t               base;
    uint64_t               mask;    /* the low mask_bits bits */
    lcc_event_on_report_fn on_report;
    lcc_event_state_fn     state;
    bool                   used;
} lcc_event_range_t;

static lcc_event_reg_t   g_table[LCC_MAX_EVENT_REGISTRATIONS];
static lcc_event_range_t g_ranges[LCC_MAX_EVENT_RANGES];

void lcc_events_reset(void)
{
    memset(g_table, 0, sizeof(g_table));
    memset(g_ranges, 0, sizeof(g_ranges));
}

bool lcc_events_register(lcc_node_t *node,
                         uint64_t event_id,
                         uint8_t flags,
                         lcc_event_on_report_fn on_report,
                         lcc_event_state_fn state)
{
    for (int i = 0; i < LCC_MAX_EVENT_REGISTRATIONS; i++) {
        if (!g_table[i].used) {
            g_table[i].node      = node;
            g_table[i].event_id  = event_id;
            g_table[i].flags     = flags;
            g_table[i].on_report = on_report;
            g_table[i].state     = state;
            g_table[i].used      = true;
            return true;
        }
    }
    return false;
}

bool lcc_events_register_consumer_range(lcc_node_t *node,
                                        uint64_t base,
                                        uint8_t mask_bits,
                                        lcc_event_on_report_fn on_report,
                                        lcc_event_state_fn state)
{
    for (int i = 0; i < LCC_MAX_EVENT_RANGES; i++) {
        if (!g_ranges[i].used) {
            g_ranges[i].node      = node;
            g_ranges[i].base      = base;
            g_ranges[i].mask      = (1ULL << mask_bits) - 1;
            g_ranges[i].on_report = on_report;
            g_ranges[i].state     = state;
            g_ranges[i].used      = true;
            return true;
        }
    }
    return false;
}

/* The node's range holding `event_id`, or NULL. */
static const lcc_event_range_t *find_range(const lcc_node_t *node,
                                           uint64_t event_id)
{
    for (int i = 0; i < LCC_MAX_EVENT_RANGES; i++) {
        const lcc_event_range_t *r = &g_ranges[i];
        if (r->used && r->node == node && (event_id & ~r->mask) == r->base)
            return r;
    }
    return NULL;
}

/* The event ID a Range Identified carries: the range's low bits all 1s,
 * or all 0s when the bit above them is 1, so the run of equal low bits
 * gives the size. */
static uint64_t range_event(const lcc_event_range_t *r)
{
    return (r->base & (r->mask + 1)) ? r->base : (r->base | r->mask);
}

static void send_global_event(const lcc_node_t *node, uint16_t mti,
                              uint64_t event_id)
{
    uint8_t buf[8];
    lcc_event_to_buf(event_id, buf);
    lcc_node_send_global(node, mti, buf, 8);
}

void lcc_events_send_report(const lcc_node_t *node, uint64_t event_id)
{
    send_global_event(node, LCC_MTI_EVENT_REPORT, event_id);
}

/* ---- identify handlers ------------------------------------------------ */

/* Send the event's Producer or Consumer Identified (`role`), carrying
 * its state: Unknown unless its registration has a state callback. */
static void send_identified(const lcc_node_t *node, uint64_t event_id,
                            lcc_event_state_fn state, uint8_t role)
{
    lcc_event_state_t st = state ? state(node, event_id)
                                 : LCC_EVENT_STATE_UNKNOWN;
    bool producer = role == LCC_EVENT_PRODUCER;
    uint16_t mti;
    switch (st) {
    case LCC_EVENT_STATE_VALID:
        mti = producer ? LCC_MTI_PRODUCER_IDENTIFIED_VALID
                       : LCC_MTI_CONSUMER_IDENTIFIED_VALID;
        break;
    case LCC_EVENT_STATE_INVALID:
        mti = producer ? LCC_MTI_PRODUCER_IDENTIFIED_INVALID
                       : LCC_MTI_CONSUMER_IDENTIFIED_INVALID;
        break;
    default:
        mti = producer ? LCC_MTI_PRODUCER_IDENTIFIED_UNKNOWN
                       : LCC_MTI_CONSUMER_IDENTIFIED_UNKNOWN;
        break;
    }
    send_global_event(node, mti, event_id);
}

/* Every train node produces Is Train. It has no table entry, since a
 * full train pool would overflow the table, and its state is Unknown,
 * as OpenMRN's FixedEventProducer sends it. */
static void send_is_train(const lcc_node_t *node)
{
    if (node->role == LCC_ROLE_TRAIN)
        send_global_event(node, LCC_MTI_PRODUCER_IDENTIFIED_UNKNOWN,
                          LCC_EVENT_IS_TRAIN);
}

static void emit_identities_for_node(const lcc_node_t *node)
{
    send_is_train(node);
    /* For each registration belonging to this node, emit the matching
     * Producer/Consumer Identified frame. */
    for (int i = 0; i < LCC_MAX_EVENT_REGISTRATIONS; i++) {
        const lcc_event_reg_t *r = &g_table[i];
        if (!r->used || r->node != node)
            continue;
        if (r->flags & LCC_EVENT_PRODUCER)
            send_identified(node, r->event_id, r->state, LCC_EVENT_PRODUCER);
        if (r->flags & LCC_EVENT_CONSUMER)
            send_identified(node, r->event_id, r->state, LCC_EVENT_CONSUMER);
    }
    for (int i = 0; i < LCC_MAX_EVENT_RANGES; i++) {
        if (g_ranges[i].used && g_ranges[i].node == node)
            send_global_event(node, LCC_MTI_CONSUMER_IDENTIFIED_RANGE,
                              range_event(&g_ranges[i]));
    }
}

void lcc_events_announce(const lcc_node_t *node)
{
    emit_identities_for_node(node);
}

static void handle_single_identify(const lcc_node_t *node,
                                   uint64_t event_id,
                                   uint8_t role)
{
    if (role == LCC_EVENT_PRODUCER && event_id == LCC_EVENT_IS_TRAIN) {
        send_is_train(node);
        return;
    }
    for (int i = 0; i < LCC_MAX_EVENT_REGISTRATIONS; i++) {
        if (!g_table[i].used || g_table[i].node != node)
            continue;
        if (!(g_table[i].flags & role))
            continue;
        if (g_table[i].event_id != event_id)
            continue;
        send_identified(node, event_id, g_table[i].state, role);
        return;
    }
    if (role == LCC_EVENT_CONSUMER) {
        const lcc_event_range_t *r = find_range(node, event_id);
        if (r)
            send_identified(node, event_id, r->state, role);
    }
}

/* ---- frame dispatch --------------------------------------------------- */

void lcc_events_handle_frame(lcc_node_t *node, const lcc_frame_t *frame)
{
    uint16_t mti = lcc_get_mti(frame->id);

    if (mti == LCC_MTI_EVENTS_IDENTIFY_GLOBAL
     || mti == LCC_MTI_EVENTS_IDENTIFY_ADDRESSED) {
        emit_identities_for_node(node);
        return;
    }

    if (mti == LCC_MTI_CONSUMER_IDENTIFY) {
        if (frame->dlc < 8) return;
        uint64_t ev = lcc_event_from_buf(frame->data);
        handle_single_identify(node, ev, LCC_EVENT_CONSUMER);
        return;
    }

    if (mti == LCC_MTI_PRODUCER_IDENTIFY) {
        if (frame->dlc < 8) return;
        uint64_t ev = lcc_event_from_buf(frame->data);
        handle_single_identify(node, ev, LCC_EVENT_PRODUCER);
        return;
    }

    if (mti == LCC_MTI_EVENT_REPORT) {
        if (frame->dlc < 8) return;
        uint64_t ev = lcc_event_from_buf(frame->data);
        for (int i = 0; i < LCC_MAX_EVENT_REGISTRATIONS; i++) {
            if (!g_table[i].used || g_table[i].node != node)
                continue;
            if (!(g_table[i].flags & LCC_EVENT_CONSUMER))
                continue;
            if (g_table[i].event_id != ev)
                continue;
            if (g_table[i].on_report)
                g_table[i].on_report(node, ev);
        }
        const lcc_event_range_t *r = find_range(node, ev);
        if (r && r->on_report)
            r->on_report(node, ev);
        return;
    }
}
