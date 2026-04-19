#include "lcc/lcc_node.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_if.h"
#include "lcc/lcc_alias.h"
#include "lcc/lcc_pip.h"
#include "lcc/lcc_snip.h"

#include <string.h>

/* ---- registry ---------------------------------------------------------- */

static lcc_node_t *g_nodes[LCC_MAX_NODES];
static int g_node_count;

void lcc_node_registry_reset(void)
{
    memset(g_nodes, 0, sizeof(g_nodes));
    g_node_count = 0;
}

bool lcc_node_register(lcc_node_t *node)
{
    if (g_node_count >= LCC_MAX_NODES)
        return false;
    g_nodes[g_node_count++] = node;
    return true;
}

int lcc_node_count(void)
{
    return g_node_count;
}

lcc_node_t *lcc_node_at(int idx)
{
    if (idx < 0 || idx >= g_node_count)
        return NULL;
    return g_nodes[idx];
}

lcc_node_t *lcc_node_find_by_alias(uint16_t alias)
{
    for (int i = 0; i < g_node_count; i++) {
        lcc_node_t *n = g_nodes[i];
        if (n->alias_state == LCC_A_CONFIRMED && n->alias == alias)
            return n;
    }
    return NULL;
}

void lcc_node_init(lcc_node_t *node, uint64_t id, lcc_node_role_t role)
{
    memset(node, 0, sizeof(*node));
    node->id          = id;
    node->role        = role;
    node->alias_state = LCC_A_IDLE;
}

/* ---- TX helpers -------------------------------------------------------- */

void lcc_node_send_global(const lcc_node_t *node, uint16_t mti,
                          const uint8_t *data, uint8_t dlc)
{
    lcc_frame_t f;
    f.id  = lcc_can_id(mti, node->alias);
    f.dlc = dlc;
    if (data && dlc > 0)
        memcpy(f.data, data, dlc);
    (void)lcc_if_tx(&f);
}

void lcc_node_send_control(const lcc_node_t *node, uint16_t field,
                           const uint8_t *data, uint8_t dlc)
{
    lcc_frame_t f;
    f.id  = lcc_control_id(node->alias, field, 0);
    f.dlc = dlc;
    if (data && dlc > 0)
        memcpy(f.data, data, dlc);
    (void)lcc_if_tx(&f);
}

void lcc_node_send_addressed(const lcc_node_t *node, uint16_t mti,
                             uint16_t dst_alias,
                             const uint8_t *payload, uint16_t payload_len)
{
    /* Addressed messages: first 2 bytes are dst alias + continuation
     * flags in upper nibble. Max 6 payload bytes per CAN frame. */
    uint16_t offset = 0;
    bool first = true;

    while (offset < payload_len || first) {
        lcc_frame_t f;
        f.id = lcc_can_id(mti, node->alias);

        uint16_t remaining = (uint16_t)(payload_len - offset);
        uint8_t  chunk     = remaining > 6 ? 6 : (uint8_t)remaining;
        bool     last      = (uint16_t)(offset + chunk) >= payload_len;

        uint8_t flags = 0;
        if (!first) flags |= LCC_NOT_FIRST_FRAME;
        if (!last)  flags |= LCC_NOT_LAST_FRAME;

        lcc_frame_build_addressed_hdr(f.data, dst_alias, flags);
        if (chunk > 0)
            memcpy(&f.data[2], payload + offset, chunk);
        f.dlc = (uint8_t)(2 + chunk);

        (void)lcc_if_tx(&f);
        offset = (uint16_t)(offset + chunk);
        first = false;
    }
}

/* ---- per-node frame handlers ------------------------------------------ */

static void handle_verify_node_id(const lcc_node_t *node, const lcc_frame_t *f)
{
    uint16_t mti = lcc_get_mti(f->id);

    /* Global variant optionally carries a 6-byte target node ID; reply only
     * if the ID matches (or is absent, meaning "everybody answer"). */
    if (mti == LCC_MTI_VERIFY_NODE_ID_GLOBAL && f->dlc == 6) {
        if (lcc_node_id_from_buf(f->data) != node->id)
            return;
    }

    uint8_t nid[6];
    lcc_node_id_to_buf(node->id, nid);
    lcc_node_send_global(node, LCC_MTI_VERIFIED_NODE_ID, nid, 6);
}

static void handle_ame(const lcc_node_t *node, const lcc_frame_t *f)
{
    /* AME may carry a 6-byte node ID filter; only respond on match. */
    if (f->dlc == 6 && lcc_node_id_from_buf(f->data) != node->id)
        return;

    uint8_t nid[6];
    lcc_node_id_to_buf(node->id, nid);
    lcc_node_send_control(node, LCC_CONTROL_AMD, nid, 6);
}

static void handle_control(lcc_node_t *node, const lcc_frame_t *f)
{
    /* Control-frame src-alias collision with our candidate/confirmed
     * alias: hand it to the alias state machine. */
    if (lcc_get_src(f->id) == node->alias)
        lcc_alias_on_collision(node);

    if (node->alias_state != LCC_A_CONFIRMED)
        return;

    uint16_t field = lcc_get_control_field(f->id);
    if (field == LCC_CONTROL_AME)
        handle_ame(node, f);
}

static bool addressed_dst_matches(const lcc_node_t *node, const lcc_frame_t *f)
{
    if (f->dlc < 2) return false;
    return lcc_frame_addressed_dst(f) == node->alias;
}

void lcc_node_handle_frame(lcc_node_t *node, const lcc_frame_t *frame)
{
    if (lcc_is_control_frame(frame->id)) {
        handle_control(node, frame);
        return;
    }

    /* Non-control frames only matter once the node owns its alias. */
    if (node->alias_state != LCC_A_CONFIRMED)
        return;

    uint8_t ftype = lcc_get_can_frame_type(frame->id);
    if (ftype != LCC_FRAME_GLOBAL_ADDRESSED)
        return;  /* datagrams handled in M4 */

    uint16_t mti = lcc_get_mti(frame->id);

    if (mti & LCC_MTI_ADDRESS_MASK) {
        /* Addressed — drop anything not pointed at us. */
        if (!addressed_dst_matches(node, frame))
            return;
    }

    switch (mti) {
    case LCC_MTI_VERIFY_NODE_ID_GLOBAL:
    case LCC_MTI_VERIFY_NODE_ID_ADDRESSED:
        handle_verify_node_id(node, frame);
        break;
    case LCC_MTI_PROTOCOL_SUPPORT_INQUIRY:
        lcc_pip_handle(node, frame);
        break;
    case LCC_MTI_IDENT_INFO_REQUEST:
        lcc_snip_handle(node, frame);
        break;
    default:
        /* Events, traction, datagrams arrive in M4+. Silently drop so
         * the bus stays quiet until those handlers land. */
        break;
    }
}
