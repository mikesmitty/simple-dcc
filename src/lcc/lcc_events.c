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

static lcc_event_reg_t g_table[LCC_MAX_EVENT_REGISTRATIONS];

void lcc_events_reset(void)
{
    memset(g_table, 0, sizeof(g_table));
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

/* Send the registration's Producer or Consumer Identified (`role`),
 * carrying its state: Unknown unless it registered a state callback. */
static void send_identified(const lcc_event_reg_t *r, uint8_t role)
{
    lcc_event_state_t st = r->state ? r->state(r->node, r->event_id)
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
    send_global_event(r->node, mti, r->event_id);
}

static void emit_identities_for_node(const lcc_node_t *node)
{
    /* For each registration belonging to this node, emit the matching
     * Producer/Consumer Identified frame. */
    for (int i = 0; i < LCC_MAX_EVENT_REGISTRATIONS; i++) {
        if (!g_table[i].used || g_table[i].node != node)
            continue;
        if (g_table[i].flags & LCC_EVENT_PRODUCER)
            send_identified(&g_table[i], LCC_EVENT_PRODUCER);
        if (g_table[i].flags & LCC_EVENT_CONSUMER)
            send_identified(&g_table[i], LCC_EVENT_CONSUMER);
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
    for (int i = 0; i < LCC_MAX_EVENT_REGISTRATIONS; i++) {
        if (!g_table[i].used || g_table[i].node != node)
            continue;
        if (!(g_table[i].flags & role))
            continue;
        if (g_table[i].event_id != event_id)
            continue;
        send_identified(&g_table[i], role);
        return;
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
        return;
    }
}
