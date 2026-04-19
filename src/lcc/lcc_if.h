#ifndef LCC_IF_H
#define LCC_IF_H

#include <stdint.h>
#include <stdbool.h>
#include "lcc/lcc_frame.h"

/*
 * Shared "CAN interface" over USB CDC GridConnect. Owns the RX queue, the
 * GridConnect parser, and the mutexes that serialize alias allocation and
 * TX across all nodes on the interface. The per-node dispatcher (fan-out
 * by alias) is filled in at M3 when node support lands; for M2 the RX
 * path simply enqueues parsed frames and the stack stays silent on the
 * bus.
 */

/* Queue depth: frames parsed from USB but not yet consumed by lcc_task. */
#ifndef LCC_IF_RX_Q_LEN
#define LCC_IF_RX_Q_LEN  16
#endif

/* Initialize queues, mutexes, and parser state. Must be called after
 * FreeRTOS primitives are available (i.e. before vTaskStartScheduler). */
void lcc_if_init(void);

/* Feed a single USB CDC byte through the GridConnect parser. On a
 * completed frame, the frame is enqueued for the protocol task. Called
 * from task_serial context. */
void lcc_if_on_rx_byte(uint8_t ch);

/* Block until a frame is available or the timeout elapses. Returns true
 * if *out was populated. Called from lcc_task context. */
bool lcc_if_rx(lcc_frame_t *out, uint32_t timeout_ms);

/* Encode `frame` as GridConnect and write to USB CDC. Serialized via the
 * internal tx mutex. Returns false only on encode failure; a disconnected
 * USB is treated as a successful silent drop so TX paths never block on
 * the host. */
bool lcc_if_tx(const lcc_frame_t *frame);

/* Serialize alias allocation / CID emission across nodes. Per-node alias
 * state machines take this before entering CID and release it after
 * CONFIRMED / CONFLICT-retry. */
void lcc_if_alias_lock(void);
void lcc_if_alias_unlock(void);

/* Fan a single RX frame out to the registered nodes based on frame type:
 * - Control frames → every node (for collision detection + AME reply).
 * - Global OpenLCB frames → every node.
 * - Addressed OpenLCB frames → the node whose alias matches the dst.
 * - Datagram frames → the node whose alias matches the datagram dst.
 * Intended to run in lcc_task context. */
void lcc_if_dispatch(const lcc_frame_t *frame);

#endif /* LCC_IF_H */
