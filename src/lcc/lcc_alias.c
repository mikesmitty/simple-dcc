#include "lcc/lcc_alias.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_if.h"

#include <string.h>

#define ALIAS_WAIT_TICKS   2   /* 2 ticks × 100 ms = 200 ms silent wait */
#define ALIAS_MAX_RETRIES  10

uint16_t lcc_alias_seed(uint64_t node_id)
{
    uint16_t a = (node_id >> 36) & 0xFFF;
    uint16_t b = (node_id >> 24) & 0xFFF;
    uint16_t c = (node_id >> 12) & 0xFFF;
    uint16_t d = (node_id >>  0) & 0xFFF;
    uint16_t s = (uint16_t)(a ^ b ^ c ^ d) & 0xFFF;
    return s ? s : 1;
}

uint16_t lcc_alias_next(uint16_t alias)
{
    uint16_t bit = ((alias >> 0) & 1) ^ ((alias >> 2) & 1)
                 ^ ((alias >> 3) & 1) ^ ((alias >> 11) & 1);
    alias = (uint16_t)((alias >> 1) | (bit << 11));
    if (alias == 0)
        alias = 1;
    return (uint16_t)(alias & 0xFFF);
}

static void emit_cid_frames(const lcc_node_t *node)
{
    /* 4 control frames with sequence 7,6,5,4; each carries 12 bits of the
     * node ID in the control-field. OpenMRN / OpenLCB spec §6.2.2. */
    for (int seq = 7; seq >= 4; seq--) {
        int shift = (seq - 4) * 12;
        uint16_t field = (uint16_t)((node->id >> shift) & 0xFFF);
        lcc_frame_t f;
        f.id  = lcc_control_id(node->alias, field, (uint8_t)seq);
        f.dlc = 0;
        (void)lcc_if_tx(&f);
    }
}

static void emit_claim_and_init(const lcc_node_t *node)
{
    /* RID: alias claim */
    lcc_node_send_control(node, LCC_CONTROL_RID, NULL, 0);

    /* AMD: announce alias-node-id mapping */
    uint8_t nid[6];
    lcc_node_id_to_buf(node->id, nid);
    lcc_node_send_control(node, LCC_CONTROL_AMD, nid, 6);

    /* INITIALIZATION_COMPLETE: node-id payload */
    lcc_node_send_global(node, LCC_MTI_INITIALIZATION_COMPLETE, nid, 6);
}

void lcc_alias_tick(lcc_node_t *node)
{
    switch (node->alias_state) {
    case LCC_A_IDLE:
        if (node->alias == 0)
            node->alias = lcc_alias_seed(node->id);

        /* Only one node at a time can hold the CID-in-flight slot. The
         * tick loop walks all nodes in a single task, so a blocking take
         * here would deadlock against the holder we're trying to advance
         * later in the same loop. */
        if (!lcc_if_alias_try_lock())
            break;
        emit_cid_frames(node);
        node->alias_state = LCC_A_CID;
        node->alias_wait_ticks = 0;
        break;

    case LCC_A_CID:
        /* One tick between CID emission and WAIT entry to let the bus
         * settle. (CONFLICT from dispatcher preempts this transition.) */
        node->alias_state = LCC_A_WAIT;
        node->alias_wait_ticks = 0;
        break;

    case LCC_A_WAIT:
        node->alias_wait_ticks++;
        if (node->alias_wait_ticks >= ALIAS_WAIT_TICKS) {
            emit_claim_and_init(node);
            node->alias_state = LCC_A_CONFIRMED;
            lcc_if_alias_unlock();
        }
        break;

    case LCC_A_CONFLICT:
        node->alias_retries++;
        lcc_if_alias_unlock();
        if (node->alias_retries >= ALIAS_MAX_RETRIES) {
            /* Give up — node stays in CONFLICT; caller can inspect and
             * decide what to do (log, reboot, etc.). */
            break;
        }
        node->alias = lcc_alias_next(node->alias);
        node->alias_state = LCC_A_IDLE;
        break;

    case LCC_A_CONFIRMED:
    default:
        break;
    }
}

void lcc_alias_on_collision(lcc_node_t *node)
{
    switch (node->alias_state) {
    case LCC_A_CID:
    case LCC_A_WAIT:
        node->alias_state = LCC_A_CONFLICT;
        break;
    case LCC_A_CONFIRMED:
        /* Defend by re-sending RID. */
        lcc_node_send_control(node, LCC_CONTROL_RID, NULL, 0);
        break;
    default:
        break;
    }
}
