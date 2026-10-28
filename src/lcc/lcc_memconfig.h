#ifndef LCC_MEMCONFIG_H
#define LCC_MEMCONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "lcc/lcc_node.h"

/*
 * Memory Configuration protocol (datagram type 0x20) — the transport JMRI
 * uses for CDI-driven configuration. A registration table of
 * lcc_memspace_handler_t entries lets the glue layer (e.g. the CS node's
 * config memory + ACDI projection) publish spaces without the protocol
 * core knowing their backing stores.
 *
 * Read/Write bounds, GET_SPACE_INFO replies, UPDATE_COMPLETE callbacks,
 * and REBOOT/FACTORY_RESET flow through this module; the handler table
 * turns each space number into concrete read()/write() calls.
 */

/* Maximum number of memory spaces a node can advertise. Bumped beyond
 * the 4 actually used so the CS node can add traction/config segments
 * without having to grow the table. */
#ifndef LCC_MAX_MEMSPACES
#define LCC_MAX_MEMSPACES  8
#endif

/* Read `count` bytes from `space` at `addr` into `out`. Return the
 * number of bytes actually produced (may be less than `count` near the
 * end of the space). Implementations must never write past `out`. If the
 * address can't be read at all, set `*error` (an OpenLCB error code; it
 * starts at 0) and the requester gets a Read Reply Failed carrying it.
 * A space with a `defer` hook may instead set `*error` to
 * LCC_MEMCFG_DEFER: the read is then handed to `defer`, so a space can
 * answer some addresses at once and others later. */
typedef uint16_t (*lcc_memspace_read_fn)(lcc_node_t *node,
                                         uint32_t addr, uint16_t count,
                                         uint8_t *out, uint16_t *error);

/* Write `count` bytes from `in` to `space` at `addr`. Return the number
 * of bytes accepted; if that's fewer than `count`, the rest is offered
 * again at the following address. If the address can't be written, set
 * `*error` (it starts at 0): the requester gets a Write Reply Failed
 * carrying it. Read-only spaces should set this to NULL. */
typedef uint16_t (*lcc_memspace_write_fn)(lcc_node_t *node,
                                          uint32_t addr, uint16_t count,
                                          const uint8_t *in, uint16_t *error);

/* Called after a successful Indicate Configuration Update Complete
 * (0xA8). Backing store may commit pending changes (e.g. flash flush). */
typedef void (*lcc_memspace_update_fn)(lcc_node_t *node);

/* A read or write handed to a space's `defer` hook. `data` points into the
 * inbound datagram and is only valid for the duration of the call. */
typedef struct {
    bool           is_write;
    uint8_t        space;
    uint32_t       addr;
    uint8_t        count;   /* read: bytes requested; write: bytes supplied */
    const uint8_t *data;    /* write payload; NULL for reads */
} lcc_memconfig_deferred_t;

/* Start a read/write whose result arrives later (e.g. a Service Mode CV
 * op on the programming track). Return 0 to accept — the owner must then
 * call lcc_memconfig_complete() exactly once, from any task. Any other
 * return value is an OpenLCB error code sent back immediately. */
typedef uint16_t (*lcc_memspace_defer_fn)(lcc_node_t *node,
                                          const lcc_memconfig_deferred_t *req);

/* Not an OpenLCB error code: a `read` handler's way of passing a request
 * on to the space's `defer` hook. */
#define LCC_MEMCFG_DEFER  0xFFFFu

typedef struct lcc_memspace_handler {
    uint8_t                space;     /* 0xFF / 0xFE / 0xFD / 0xFC / 0xFB / 0xF8 */
    uint32_t               size;      /* bytes; highest-address reply = size-1 */
    bool                   read_only;
    lcc_memspace_read_fn   read;
    lcc_memspace_write_fn  write;     /* may be NULL iff read_only */
    lcc_memspace_update_fn update_complete;  /* may be NULL */
    /* When non-NULL, requests are ACKed with Reply Pending and handed here
     * instead of to read/write: all reads if `read` is NULL, otherwise the
     * ones `read` passes on with LCC_MEMCFG_DEFER; likewise all writes if
     * `write` is NULL. Only one deferred request is in flight stack-wide;
     * others get a temporary error until it ends. */
    lcc_memspace_defer_fn  defer;
} lcc_memspace_handler_t;

/* Timeout exponent advertised with Reply Pending for deferred requests:
 * the requester waits 2^N seconds for the reply before retrying. */
#define LCC_MEMCFG_DEFER_TIMEOUT_EXP  3

/* Replace the current global space table. The referenced entries must
 * outlive the LCC stack (static storage). Intended to be called once
 * from the glue layer's init routine. */
void lcc_memconfig_register_spaces(const lcc_memspace_handler_t *spaces,
                                   uint8_t count);

/* Attach a per-node space table. Per-node spaces take precedence over
 * global ones when both declare the same space id, so a node can e.g.
 * expose a traction-CV space (0xF8) without advertising it on the CS
 * node. The table must outlive the node. */
void lcc_memconfig_register_node_spaces(lcc_node_t *node,
                                        const lcc_memspace_handler_t *spaces,
                                        uint8_t count);

/* Callback type for factory reset: the glue layer clears its backing
 * config store and reboots. NULL disables 0xAA. */
typedef void (*lcc_memconfig_factory_reset_fn)(lcc_node_t *node);

/* Callback type for reboot: the glue layer triggers a watchdog reset
 * (or equivalent). NULL disables 0xA9. */
typedef void (*lcc_memconfig_reboot_fn)(void);

/* Wire reboot + factory-reset callbacks. Either may be NULL; the
 * corresponding command is then rejected as unimplemented. */
void lcc_memconfig_set_hooks(lcc_memconfig_reboot_fn reboot,
                             lcc_memconfig_factory_reset_fn factory_reset);

/* Vet a memconfig datagram before lcc_datagram acknowledges it. Returns 0
 * to accept: send Datagram OK carrying *ok_flags (Reply Pending for every
 * read and write, plus LCC_MEMCFG_DEFER_TIMEOUT_EXP on a deferred space;
 * 0 for other commands), then call lcc_memconfig_handle. Any other value
 * is the error code to send in Datagram Rejected (unknown space, read-only
 * space, malformed or unimplemented command), and the datagram must not be
 * handled. */
uint16_t lcc_memconfig_check(const lcc_node_t *node, const uint8_t *payload,
                             uint8_t len, uint8_t *ok_flags);

/* Entry point from lcc_datagram for a datagram lcc_memconfig_check
 * accepted: `payload` is the full datagram starting with the 0x20 protocol
 * byte; `len` is its length. */
void lcc_memconfig_handle(lcc_node_t *node, uint16_t src_alias,
                          const uint8_t *payload, uint8_t len);

/* Finish the in-flight deferred request. `error` 0 means success; for
 * reads, `data`/`len` carry the bytes read. Safe from any task — the reply
 * itself goes out from lcc_memconfig_flush_deferred(). Ignored if no
 * deferred request is pending. */
void lcc_memconfig_complete(uint16_t error, const uint8_t *data, uint8_t len);

/* Send the reply for a completed deferred request, if any. Called from
 * the LCC task loop so replies share that task's TX path. */
void lcc_memconfig_flush_deferred(void);

/* True from the moment a deferred request is accepted until its reply has
 * gone out. The LCC task polls faster while this holds. */
bool lcc_memconfig_deferred_pending(void);

#endif /* LCC_MEMCONFIG_H */
