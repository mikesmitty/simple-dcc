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
 * end of the space). Implementations must never write past `out`. */
typedef uint16_t (*lcc_memspace_read_fn)(lcc_node_t *node,
                                         uint32_t addr, uint16_t count,
                                         uint8_t *out);

/* Write `count` bytes from `in` to `space` at `addr`. Return the number
 * of bytes accepted. Read-only spaces should set this to NULL. */
typedef uint16_t (*lcc_memspace_write_fn)(lcc_node_t *node,
                                          uint32_t addr, uint16_t count,
                                          const uint8_t *in);

/* Called after a successful Indicate Configuration Update Complete
 * (0xA8). Backing store may commit pending changes (e.g. flash flush). */
typedef void (*lcc_memspace_update_fn)(lcc_node_t *node);

typedef struct {
    uint8_t                space;     /* 0xFF / 0xFE / 0xFD / 0xFC / 0xFB */
    uint32_t               size;      /* bytes; highest-address reply = size-1 */
    bool                   read_only;
    lcc_memspace_read_fn   read;
    lcc_memspace_write_fn  write;     /* may be NULL iff read_only */
    lcc_memspace_update_fn update_complete;  /* may be NULL */
} lcc_memspace_handler_t;

/* Replace the current space table. The referenced entries must outlive
 * the LCC stack (static storage). Intended to be called once from the
 * glue layer's init routine. */
void lcc_memconfig_register_spaces(const lcc_memspace_handler_t *spaces,
                                   uint8_t count);

/* Callback type for factory reset: the glue layer clears its backing
 * config store and reboots. NULL disables 0xAA. */
typedef void (*lcc_memconfig_factory_reset_fn)(lcc_node_t *node);

/* Callback type for reboot: the glue layer triggers a watchdog reset
 * (or equivalent). NULL disables 0xA9. */
typedef void (*lcc_memconfig_reboot_fn)(void);

/* Wire reboot + factory-reset callbacks. Either may be NULL; the
 * corresponding command will be silently ignored. */
void lcc_memconfig_set_hooks(lcc_memconfig_reboot_fn reboot,
                             lcc_memconfig_factory_reset_fn factory_reset);

/* Entry point from lcc_datagram: `payload` is the full datagram starting
 * with the 0x20 protocol byte; `len` is its length. */
void lcc_memconfig_handle(lcc_node_t *node, uint16_t src_alias,
                          const uint8_t *payload, uint8_t len);

#endif /* LCC_MEMCONFIG_H */
