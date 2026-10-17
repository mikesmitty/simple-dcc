#include "lcc/lcc_memconfig.h"
#include "lcc/lcc_datagram.h"
#include "lcc/lcc_defs.h"

#include <string.h>

/* ---- registered space table ------------------------------------------ */

static const lcc_memspace_handler_t *g_spaces;
static uint8_t                       g_space_count;
static lcc_memconfig_reboot_fn        g_reboot;
static lcc_memconfig_factory_reset_fn g_factory_reset;

void lcc_memconfig_register_spaces(const lcc_memspace_handler_t *spaces,
                                   uint8_t count)
{
    g_spaces      = spaces;
    g_space_count = (count > LCC_MAX_MEMSPACES) ? LCC_MAX_MEMSPACES : count;
}

void lcc_memconfig_register_node_spaces(lcc_node_t *node,
                                        const lcc_memspace_handler_t *spaces,
                                        uint8_t count)
{
    node->node_spaces      = spaces;
    node->node_space_count = (count > LCC_MAX_MEMSPACES)
                                 ? LCC_MAX_MEMSPACES
                                 : count;
}

void lcc_memconfig_set_hooks(lcc_memconfig_reboot_fn reboot,
                             lcc_memconfig_factory_reset_fn factory_reset)
{
    g_reboot        = reboot;
    g_factory_reset = factory_reset;
}

/* Per-node spaces win over global spaces when both declare the same id.
 * Lets a train node advertise 0xF8 (traction CVs) without the CS node
 * also exposing it. */
static const lcc_memspace_handler_t *find_space(const lcc_node_t *node,
                                                uint8_t space)
{
    if (node && node->node_spaces) {
        for (uint8_t i = 0; i < node->node_space_count; i++) {
            if (node->node_spaces[i].space == space)
                return &node->node_spaces[i];
        }
    }
    for (uint8_t i = 0; i < g_space_count; i++) {
        if (g_spaces[i].space == space)
            return &g_spaces[i];
    }
    return NULL;
}

/* ---- request decoder -------------------------------------------------- */

/*
 * Read/Write sub-commands encode the space number in the low 2 bits:
 *   0x03 → 0xFF   (CDI)
 *   0x02 → 0xFE   (ALL_MEMORY)
 *   0x01 → 0xFD   (CONFIG)
 *   0x00 → explicit space byte follows the 4-byte address.
 *
 * Address is always bytes [2..5] big-endian. `data_offset` is the index
 * of the first post-header payload byte (count on reads, data on writes).
 */
static uint8_t decode_space(uint8_t cmd, const uint8_t *buf, uint8_t len,
                            uint32_t *addr, uint8_t *data_offset)
{
    *addr = ((uint32_t)buf[2] << 24) | ((uint32_t)buf[3] << 16)
          | ((uint32_t)buf[4] <<  8) |  (uint32_t)buf[5];

    uint8_t space_bits = cmd & 0x03;
    if (space_bits != 0) {
        *data_offset = 6;
        return (uint8_t)(0xFC + space_bits);
    }
    *data_offset = 7;
    return (len > 6) ? buf[6] : (uint8_t)0;
}

/* ---- read/write replies ----------------------------------------------- */

/* Build the Read Reply / Write Reply for a request whose sub-command byte
 * was `cmd`. A nonzero `error` sets the failure bit and carries the code
 * in place of data. Space encoding (low bits vs. explicit byte) mirrors
 * the request so the requester can match it up. */
static void send_reply(lcc_node_t *node, uint16_t dst_alias, uint8_t cmd,
                       uint8_t space, uint32_t addr, uint16_t error,
                       const uint8_t *data, uint8_t len)
{
    bool    is_write   = (cmd & 0xF0) == LCC_MEMCFG_WRITE;
    uint8_t space_bits = cmd & 0x03;

    uint8_t reply[LCC_DATAGRAM_MAX];
    uint8_t rpos = 0;
    reply[rpos++] = LCC_MEMCONFIG_CMD;
    reply[rpos++] = (uint8_t)((is_write ? LCC_MEMCFG_WRITE_REPLY
                                        : LCC_MEMCFG_READ_REPLY)
                              | (error ? LCC_MEMCFG_REPLY_FAILED : 0)
                              | space_bits);
    reply[rpos++] = (uint8_t)((addr >> 24) & 0xFF);
    reply[rpos++] = (uint8_t)((addr >> 16) & 0xFF);
    reply[rpos++] = (uint8_t)((addr >>  8) & 0xFF);
    reply[rpos++] = (uint8_t)( addr        & 0xFF);
    if (space_bits == 0)
        reply[rpos++] = space;

    if (error) {
        reply[rpos++] = (uint8_t)(error >> 8);
        reply[rpos++] = (uint8_t)(error & 0xFF);
    } else if (!is_write && data) {
        uint8_t room = (uint8_t)(sizeof(reply) - rpos);
        if (len > room) len = room;
        memcpy(&reply[rpos], data, len);
        rpos = (uint8_t)(rpos + len);
    }

    lcc_datagram_send(node, dst_alias, reply, rpos);
}

/* ---- deferred requests ------------------------------------------------ */

/* One slot: the only deferred space today is the programming track, which
 * can run a single CV op at a time anyway. State moves IDLE → PENDING on
 * the LCC task, PENDING → DONE on whichever task finishes the op, and
 * DONE → IDLE back on the LCC task once the reply is out. */
enum {
    DEFERRED_IDLE,
    DEFERRED_PENDING,
    DEFERRED_DONE,
};

static struct {
    uint8_t     state;
    lcc_node_t *node;
    uint16_t    src_alias;
    uint8_t     cmd;
    uint8_t     space;
    uint32_t    addr;
    uint16_t    error;
    uint8_t     len;
    uint8_t     data[LCC_DATAGRAM_MAX - 7];  /* 7 = reply header w/ space byte */
} g_deferred;

static void start_deferred(lcc_node_t *node, uint16_t src_alias,
                           uint8_t cmd, uint8_t space, uint32_t addr,
                           uint8_t count, const uint8_t *data,
                           const lcc_memspace_handler_t *h)
{
    uint16_t error;
    if (addr >= h->size) {
        error = LCC_ERR_OUT_OF_BOUNDS;
    } else if (__atomic_load_n(&g_deferred.state, __ATOMIC_ACQUIRE)
               != DEFERRED_IDLE) {
        error = LCC_ERR_TEMPORARY;
    } else {
        g_deferred.node      = node;
        g_deferred.src_alias = src_alias;
        g_deferred.cmd       = cmd;
        g_deferred.space     = space;
        g_deferred.addr      = addr;
        g_deferred.error     = 0;
        g_deferred.len       = 0;
        __atomic_store_n(&g_deferred.state, DEFERRED_PENDING, __ATOMIC_RELEASE);

        lcc_memconfig_deferred_t req = {
            .is_write = data != NULL,
            .space    = space,
            .addr     = addr,
            .count    = count,
            .data     = data,
        };
        error = h->defer(node, &req);
        if (error == 0)
            return;
        __atomic_store_n(&g_deferred.state, DEFERRED_IDLE, __ATOMIC_RELEASE);
    }
    send_reply(node, src_alias, cmd, space, addr, error, NULL, 0);
}

void lcc_memconfig_complete(uint16_t error, const uint8_t *data, uint8_t len)
{
    if (__atomic_load_n(&g_deferred.state, __ATOMIC_ACQUIRE)
        != DEFERRED_PENDING)
        return;
    if (len > sizeof(g_deferred.data))
        len = sizeof(g_deferred.data);
    if (data && len)
        memcpy(g_deferred.data, data, len);
    g_deferred.len   = data ? len : 0;
    g_deferred.error = error;
    __atomic_store_n(&g_deferred.state, DEFERRED_DONE, __ATOMIC_RELEASE);
}

void lcc_memconfig_flush_deferred(void)
{
    if (__atomic_load_n(&g_deferred.state, __ATOMIC_ACQUIRE) != DEFERRED_DONE)
        return;
    send_reply(g_deferred.node, g_deferred.src_alias, g_deferred.cmd,
               g_deferred.space, g_deferred.addr, g_deferred.error,
               g_deferred.data, g_deferred.len);
    __atomic_store_n(&g_deferred.state, DEFERRED_IDLE, __ATOMIC_RELEASE);
}

bool lcc_memconfig_deferred_pending(void)
{
    return __atomic_load_n(&g_deferred.state, __ATOMIC_ACQUIRE)
           != DEFERRED_IDLE;
}

/* ---- request validation ----------------------------------------------- */

static bool is_read_cmd(uint8_t cmd)
{
    return (cmd & LCC_MEMCFG_CMD_MASK) == LCC_MEMCFG_READ;
}

static bool is_write_cmd(uint8_t cmd)
{
    return (cmd & LCC_MEMCFG_CMD_MASK) == LCC_MEMCFG_WRITE;
}

typedef struct {
    uint8_t                       space;
    uint32_t                      addr;
    uint8_t                       count;  /* read: requested; write: supplied */
    const uint8_t                *data;   /* write payload; NULL for reads */
    const lcc_memspace_handler_t *h;
} rw_req_t;

/* Decode a Read or Write and decide whether it can be served. Returns 0,
 * or the error code to reject it with (same choices as OpenMRN's
 * MemoryConfigHandler). A read past the end of a synchronous space is
 * accepted and gets a short reply; on a deferred space start_deferred
 * fails it with a reply. */
static uint16_t parse_rw(const lcc_node_t *node, const uint8_t *buf,
                         uint8_t len, rw_req_t *rq)
{
    if (len < 7)
        return LCC_ERR_INVALID_ARGUMENTS;

    uint8_t data_offset;
    rq->space = decode_space(buf[1], buf, len, &rq->addr, &data_offset);
    if (data_offset >= len)
        return LCC_ERR_INVALID_ARGUMENTS;
    rq->h = find_space(node, rq->space);

    if (is_read_cmd(buf[1])) {
        rq->count = buf[data_offset];
        rq->data  = NULL;
        if (rq->count == 0)
            return LCC_ERR_INVALID_ARGUMENTS;
        if (!rq->h || (!rq->h->defer && !rq->h->read))
            return LCC_ERR_SPACE_UNKNOWN;
        return 0;
    }

    rq->count = (uint8_t)(len - data_offset);
    rq->data  = buf + data_offset;
    if (!rq->h)
        return LCC_ERR_SPACE_UNKNOWN;
    if (rq->h->read_only || (!rq->h->defer && !rq->h->write))
        return LCC_ERR_WRITE_READ_ONLY;
    if (!rq->h->defer && rq->addr >= rq->h->size)
        return LCC_ERR_OUT_OF_BOUNDS;
    return 0;
}

/* Spec requires the 6-byte node ID as confirmation. */
static bool factory_reset_confirmed(const lcc_node_t *node,
                                    const uint8_t *buf, uint8_t len)
{
    if (len < 8)
        return false;
    uint64_t confirm = ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32)
                     | ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16)
                     | ((uint64_t)buf[6] <<  8) |  (uint64_t)buf[7];
    return confirm == node->id;
}

uint16_t lcc_memconfig_check(const lcc_node_t *node, const uint8_t *payload,
                             uint8_t len, uint8_t *ok_flags)
{
    *ok_flags = 0;
    if (len < 2)
        return LCC_ERR_INVALID_ARGUMENTS;

    uint8_t cmd = payload[1];
    if (is_read_cmd(cmd) || is_write_cmd(cmd)) {
        rw_req_t rq;
        uint16_t error = parse_rw(node, payload, len, &rq);
        if (error)
            return error;
        if (rq.h->defer)
            *ok_flags = LCC_DATAGRAM_OK_REPLY_PENDING
                      | LCC_MEMCFG_DEFER_TIMEOUT_EXP;
        return 0;
    }

    switch (cmd) {
    case LCC_MEMCFG_GET_SPACE_INFO:
        return len < 3 ? LCC_ERR_INVALID_ARGUMENTS : 0;
    case LCC_MEMCFG_UPDATE_COMPLETE:
        return 0;
    case LCC_MEMCFG_REBOOT:
        return g_reboot ? 0 : LCC_ERR_UNIMPLEMENTED_SUBCMD;
    case LCC_MEMCFG_FACTORY_RESET:
        if (!g_factory_reset)
            return LCC_ERR_UNIMPLEMENTED_SUBCMD;
        return factory_reset_confirmed(node, payload, len)
             ? 0 : LCC_ERR_INVALID_ARGUMENTS;
    default:
        /* Includes replies (we are never a memconfig client), options,
         * lock, and freeze. */
        return LCC_ERR_UNIMPLEMENTED_SUBCMD;
    }
}

/* ---- GET_SPACE_INFO --------------------------------------------------- */

static void handle_get_space_info(lcc_node_t *node, uint16_t src_alias,
                                  const uint8_t *buf, uint8_t len)
{
    if (len < 3)
        return;
    uint8_t space = buf[2];
    const lcc_memspace_handler_t *h = find_space(node, space);

    uint8_t reply[8];
    reply[0] = LCC_MEMCONFIG_CMD;

    if (!h) {
        reply[1] = LCC_MEMCFG_SPACE_NOT_KNOWN;
        reply[2] = space;
        lcc_datagram_send(node, src_alias, reply, 3);
        return;
    }

    uint32_t highest = (h->size > 0) ? (h->size - 1) : 0;
    reply[1] = LCC_MEMCFG_SPACE_INFO_REPLY;
    reply[2] = space;
    reply[3] = (uint8_t)((highest >> 24) & 0xFF);
    reply[4] = (uint8_t)((highest >> 16) & 0xFF);
    reply[5] = (uint8_t)((highest >>  8) & 0xFF);
    reply[6] = (uint8_t)( highest        & 0xFF);
    /* flags: bit 0 = read-only. Present-bit is implied by
     * SPACE_INFO_REPLY (vs. SPACE_NOT_KNOWN). */
    reply[7] = h->read_only ? 0x01 : 0x00;
    lcc_datagram_send(node, src_alias, reply, 8);
}

/* ---- READ ------------------------------------------------------------- */

static void handle_read(lcc_node_t *node, uint16_t src_alias,
                        const uint8_t *buf, uint8_t len)
{
    /* Anything parse_rw refuses was already rejected by
     * lcc_memconfig_check. */
    rw_req_t rq;
    if (parse_rw(node, buf, len, &rq) != 0)
        return;
    const lcc_memspace_handler_t *h = rq.h;
    uint8_t  space = rq.space;
    uint32_t addr  = rq.addr;
    uint8_t  count = rq.count;

    if (h->defer) {
        start_deferred(node, src_alias, buf[1], space, addr, count, NULL, h);
        return;
    }

    /* Clamp to space bounds. */
    if (addr >= h->size)
        count = 0;
    else if ((uint32_t)addr + count > h->size)
        count = (uint8_t)(h->size - addr);

    uint8_t reply[72];
    uint8_t rpos = 0;
    reply[rpos++] = LCC_MEMCONFIG_CMD;

    uint8_t space_bits = buf[1] & 0x03;
    reply[rpos++] = (uint8_t)(LCC_MEMCFG_READ_REPLY | space_bits);

    reply[rpos++] = (uint8_t)((addr >> 24) & 0xFF);
    reply[rpos++] = (uint8_t)((addr >> 16) & 0xFF);
    reply[rpos++] = (uint8_t)((addr >>  8) & 0xFF);
    reply[rpos++] = (uint8_t)( addr        & 0xFF);

    if (space_bits == 0)
        reply[rpos++] = space;

    if (count > 0) {
        uint16_t room = (uint16_t)(sizeof(reply) - rpos);
        if (count > room) count = (uint8_t)room;
        uint16_t produced = h->read(node, addr, count, &reply[rpos]);
        rpos = (uint8_t)(rpos + produced);
    }

    lcc_datagram_send(node, src_alias, reply, rpos);
}

/* ---- WRITE ------------------------------------------------------------ */

static void handle_write(lcc_node_t *node, uint16_t src_alias,
                         const uint8_t *buf, uint8_t len)
{
    rw_req_t rq;
    if (parse_rw(node, buf, len, &rq) != 0)
        return;
    const lcc_memspace_handler_t *h = rq.h;
    uint32_t addr  = rq.addr;
    uint8_t  count = rq.count;
    const uint8_t *write_data = rq.data;

    if (h->defer) {
        start_deferred(node, src_alias, buf[1], rq.space, addr, count,
                       write_data, h);
        return;
    }

    if ((uint32_t)addr + count > h->size)
        count = (uint8_t)(h->size - addr);

    (void)src_alias;
    (void)h->write(node, addr, count, write_data);

    /* Optional WRITE_REPLY would go here — JMRI does not require it for
     * the well-known spaces and OpenMRN similarly skips it. */
}

/* ---- UPDATE_COMPLETE / REBOOT / FACTORY_RESET ------------------------- */

static void handle_update_complete(lcc_node_t *node)
{
    /* Fire update_complete on every space visible to this node (per-node
     * first, then global). Spaces that share a backing store should dedupe
     * internally. */
    if (node->node_spaces) {
        for (uint8_t i = 0; i < node->node_space_count; i++) {
            if (node->node_spaces[i].update_complete)
                node->node_spaces[i].update_complete(node);
        }
    }
    for (uint8_t i = 0; i < g_space_count; i++) {
        if (g_spaces[i].update_complete)
            g_spaces[i].update_complete(node);
    }
}

/* lcc_datagram has already sent the Datagram OK for these. */
static void handle_reboot(void)
{
    if (g_reboot)
        g_reboot();
}

static void handle_factory_reset(lcc_node_t *node,
                                 const uint8_t *buf, uint8_t len)
{
    if (!factory_reset_confirmed(node, buf, len))
        return;
    if (g_factory_reset)
        g_factory_reset(node);
}

/* ---- top-level dispatcher -------------------------------------------- */

void lcc_memconfig_handle(lcc_node_t *node, uint16_t src_alias,
                          const uint8_t *payload, uint8_t len)
{
    if (len < 2)
        return;

    uint8_t cmd = payload[1];

    if (cmd == LCC_MEMCFG_GET_SPACE_INFO) {
        handle_get_space_info(node, src_alias, payload, len);
        return;
    }
    if (cmd == LCC_MEMCFG_UPDATE_COMPLETE) {
        handle_update_complete(node);
        return;
    }
    if (cmd == LCC_MEMCFG_REBOOT) {
        handle_reboot();
        return;
    }
    if (cmd == LCC_MEMCFG_FACTORY_RESET) {
        handle_factory_reset(node, payload, len);
        return;
    }

    /* Read commands: 0x40..0x43 (space in low 2 bits or 0 for explicit). */
    if (is_read_cmd(cmd)) {
        handle_read(node, src_alias, payload, len);
        return;
    }

    /* Write commands: 0x00..0x03. */
    if (is_write_cmd(cmd)) {
        handle_write(node, src_alias, payload, len);
        return;
    }

    /* Anything else was rejected by lcc_memconfig_check. */
}
