#ifndef LCC_IF_H
#define LCC_IF_H

#include <stdint.h>
#include <stdbool.h>
#include "lcc/lcc_frame.h"

/*
 * Shared "CAN interface" for the LCC stack. Owns the RX queue, the
 * GridConnect parser used by the USB transport, and the mutexes that
 * serialize alias allocation and TX across all nodes on the interface.
 * Multiple transports (USB CDC + GridConnect, native CAN via MCP2515, ...)
 * feed the same RX queue via `lcc_if_enqueue_frame()` and receive TX fan-out
 * via the registered egress callbacks.
 */

/* Queue depth: frames parsed from any transport but not yet consumed by
 * lcc_task. */
#ifndef LCC_IF_RX_Q_LEN
#define LCC_IF_RX_Q_LEN  16
#endif

/* Max number of egress transports that can be registered (USB + MCP2515
 * today; leave a little headroom). */
#ifndef LCC_IF_MAX_EGRESS
#define LCC_IF_MAX_EGRESS 3
#endif

/* Egress callback: transport implementations supply one of these and
 * register it at init time. Returns true on success (or on a tolerated
 * silent drop, e.g. USB not yet connected); false signals an encode/
 * hardware failure that the caller may wish to surface. */
typedef bool (*lcc_egress_fn)(const lcc_frame_t *frame, void *ctx);

/* Initialize queues, mutexes, parser state, and register the built-in
 * USB/GridConnect egress. Must be called after FreeRTOS primitives are
 * available (i.e. before vTaskStartScheduler). */
void lcc_if_init(void);

/* Register an additional egress transport. Called before scheduler start
 * (registration array is not locked). Returns false if the slot table is
 * full. */
bool lcc_if_register_transport(lcc_egress_fn fn, void *ctx);

/* Feed a single USB CDC byte through the GridConnect parser. On a
 * completed frame, the frame is enqueued via lcc_if_enqueue_frame().
 * Called from task_serial context. */
void lcc_if_on_rx_byte(uint8_t ch);

/* Transport-agnostic RX ingress. Any transport that already has a decoded
 * CAN frame (e.g. MCP2515 RX ISR + SPI read) pushes it through here.
 * Drops silently if the RX queue is full. */
void lcc_if_enqueue_frame(const lcc_frame_t *frame);

/* Block until a frame is available or the timeout elapses. Returns true
 * if *out was populated. Called from lcc_task context. */
bool lcc_if_rx(lcc_frame_t *out, uint32_t timeout_ms);

/* Fan `frame` out to every registered egress transport. Returns true if
 * at least one transport accepted the frame; false only when all
 * transports report failure. Serialized via the internal tx mutex. */
bool lcc_if_tx(const lcc_frame_t *frame);

/* Serialize alias allocation / CID emission across nodes. The tick loop
 * walks all nodes inside one task, so the IDLE→CID transition must use
 * try_lock and stay IDLE on contention — a blocking take would deadlock
 * against the holder we'd advance later in the same loop. Released by the
 * holder when it reaches CONFIRMED or CONFLICT-retry. */
bool lcc_if_alias_try_lock(void);
void lcc_if_alias_unlock(void);

/* Fan a single RX frame out to the registered nodes based on frame type:
 * - Control frames → every node (for collision detection + AME reply).
 * - Global OpenLCB frames → every node.
 * - Addressed OpenLCB frames → the node whose alias matches the dst.
 * - Datagram frames → the node whose alias matches the datagram dst.
 * Intended to run in lcc_task context. */
void lcc_if_dispatch(const lcc_frame_t *frame);

#endif /* LCC_IF_H */
