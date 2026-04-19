#ifndef LCC_EVENTS_H
#define LCC_EVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include "lcc/lcc_node.h"
#include "lcc/lcc_frame.h"

/*
 * Event Exchange protocol. A registration table maps event IDs to
 * (node, flags, consumer callback). The CS registers well-known
 * emergency events (EMERGENCY_OFF, CLEAR_EMERG_OFF, EMERGENCY_STOP,
 * CLEAR_EMERG_STOP) and acts on them when they land via
 * PC_EVENT_REPORT (MTI 0x05B4).
 *
 * Train nodes register DCC-proxy Train Search events in M7; for M4 the
 * table just carries the CS's consumer set.
 */

#ifndef LCC_MAX_EVENT_REGISTRATIONS
#define LCC_MAX_EVENT_REGISTRATIONS  16
#endif

/* Role flags — OR together for dual producer/consumer. */
#define LCC_EVENT_PRODUCER  0x01
#define LCC_EVENT_CONSUMER  0x02

/* Callback type. Fires when PC_EVENT_REPORT matches a registered event
 * and the registration has the CONSUMER flag set. */
typedef void (*lcc_event_on_report_fn)(lcc_node_t *node, uint64_t event_id);

/* Reset the registration table. Called from lcc_task_init. */
void lcc_events_reset(void);

/* Add a registration. Returns false if the table is full. */
bool lcc_events_register(lcc_node_t *node,
                         uint64_t event_id,
                         uint8_t flags,
                         lcc_event_on_report_fn on_report);

/* Dispatch an inbound event-related frame: CONSUMER_IDENTIFY (0x08F4),
 * PRODUCER_IDENTIFY (0x0914), EVENT_REPORT (0x05B4), and IDENTIFY_*
 * (0x0968 / 0x0970). Frame is global or addressed — the caller has
 * already verified the addressed destination. */
void lcc_events_handle_frame(lcc_node_t *node, const lcc_frame_t *frame);

/* Produce an EVENT_REPORT (MTI 0x05B4) with the given event ID sourced
 * from `node->alias`. Used by the emergency-event sender in M4 and the
 * traction layer in M5+. */
void lcc_events_send_report(const lcc_node_t *node, uint64_t event_id);

#endif /* LCC_EVENTS_H */
