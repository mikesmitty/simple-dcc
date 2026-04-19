#ifndef LCC_DATAGRAM_H
#define LCC_DATAGRAM_H

#include <stdint.h>
#include <stdbool.h>
#include "lcc/lcc_frame.h"
#include "lcc/lcc_node.h"

/*
 * Datagram reassembly + TX. OpenLCB datagrams are up to 72 bytes delivered
 * in 1..N CAN frames with CAN-frame-type encoding ONE / FIRST / MIDDLE /
 * FINAL (defs 2..5 in lcc_defs.h). One reassembly buffer per node lives
 * in `lcc_node_t::dg_rx`; state is reset between messages. A completed
 * datagram is ACK'd with Datagram OK and delivered to the dispatcher.
 */

/* Upper bound for a datagram payload (and our reassembly buffer). */
#define LCC_DATAGRAM_MAX  72

/* Dispatcher for a fully reassembled datagram — picks a protocol handler
 * (today: MemoryConfig 0x20) based on the first payload byte. Returns
 * true if the datagram was ACK'd and processed, false if rejected. */
bool lcc_datagram_dispatch(lcc_node_t *node, uint16_t src_alias,
                           const uint8_t *payload, uint8_t len);

/* Called by the frame dispatcher when a datagram-type frame (CAN frame
 * type 2..5) arrives for `node`. Reassembles and forwards to the
 * protocol dispatcher on completion. */
void lcc_datagram_handle_frame(lcc_node_t *node, const lcc_frame_t *frame);

/* Send Datagram OK (MTI 0x0A28) to `dst_alias`. The reply type is
 * ADDRESSED but the wire format is just the 2-byte dst header — no extra
 * payload — so this is a single CAN frame. */
void lcc_datagram_send_ok(const lcc_node_t *node, uint16_t dst_alias);

/* Send Datagram Rejected (MTI 0x0A48) with a 16-bit error code. */
void lcc_datagram_send_rejected(const lcc_node_t *node, uint16_t dst_alias,
                                uint16_t error_code);

/* Transmit `payload` as a 1..N frame datagram to `dst_alias`. Uses the
 * ONE/FIRST/MIDDLE/FINAL CAN-frame-type encoding; payload may be up to
 * LCC_DATAGRAM_MAX bytes. */
void lcc_datagram_send(const lcc_node_t *node, uint16_t dst_alias,
                       const uint8_t *payload, uint8_t len);

#endif /* LCC_DATAGRAM_H */
