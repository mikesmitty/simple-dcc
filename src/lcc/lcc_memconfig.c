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

void lcc_memconfig_set_hooks(lcc_memconfig_reboot_fn reboot,
                             lcc_memconfig_factory_reset_fn factory_reset)
{
    g_reboot        = reboot;
    g_factory_reset = factory_reset;
}

static const lcc_memspace_handler_t *find_space(uint8_t space)
{
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

/* ---- GET_SPACE_INFO --------------------------------------------------- */

static void handle_get_space_info(lcc_node_t *node, uint16_t src_alias,
                                  const uint8_t *buf, uint8_t len)
{
    if (len < 3)
        return;
    uint8_t space = buf[2];
    const lcc_memspace_handler_t *h = find_space(space);

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
    if (len < 7)
        return;

    uint32_t addr;
    uint8_t  data_offset;
    uint8_t  space = decode_space(buf[1], buf, len, &addr, &data_offset);

    if (data_offset >= len)
        return;
    uint8_t count = buf[data_offset];
    if (count == 0)
        return;

    const lcc_memspace_handler_t *h = find_space(space);
    if (!h || !h->read)
        return;

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
    if (len < 7)
        return;

    uint32_t addr;
    uint8_t  data_offset;
    uint8_t  space = decode_space(buf[1], buf, len, &addr, &data_offset);

    if (data_offset >= len)
        return;

    const lcc_memspace_handler_t *h = find_space(space);
    if (!h || h->read_only || !h->write)
        return;

    uint8_t count = (uint8_t)(len - data_offset);
    const uint8_t *write_data = buf + data_offset;

    if (addr >= h->size)
        return;
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
    /* Fire update_complete on every registered space. Spaces that share
     * a backing store should dedupe internally. */
    for (uint8_t i = 0; i < g_space_count; i++) {
        if (g_spaces[i].update_complete)
            g_spaces[i].update_complete(node);
    }
}

static void handle_reboot(lcc_node_t *node, uint16_t src_alias)
{
    lcc_datagram_send_ok(node, src_alias);
    if (g_reboot)
        g_reboot();
}

static void handle_factory_reset(lcc_node_t *node, uint16_t src_alias,
                                 const uint8_t *buf, uint8_t len)
{
    /* Spec requires the 6-byte node ID as confirmation. */
    if (len < 8)
        return;
    uint64_t confirm = ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32)
                     | ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16)
                     | ((uint64_t)buf[6] <<  8) |  (uint64_t)buf[7];
    if (confirm != node->id)
        return;

    lcc_datagram_send_ok(node, src_alias);
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
        handle_reboot(node, src_alias);
        return;
    }
    if (cmd == LCC_MEMCFG_FACTORY_RESET) {
        handle_factory_reset(node, src_alias, payload, len);
        return;
    }

    /* Read commands: 0x40..0x4F (space in low 2 bits or 0 for explicit). */
    if ((cmd & 0xF0) == 0x40) {
        handle_read(node, src_alias, payload, len);
        return;
    }

    /* Write commands: 0x00..0x0F. */
    if ((cmd & 0xF0) == 0x00) {
        handle_write(node, src_alias, payload, len);
        return;
    }

    /* Unknown memconfig sub-command — ignore. */
}
