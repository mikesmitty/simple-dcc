#include "lcc/lcc_datagram.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_if.h"
#include "lcc/lcc_memconfig.h"

#include <string.h>

/* OpenLCB datagram rejection codes — only the ones the stack actually
 * emits are named here. Full list lives in the OpenLCB Datagram S&TN. */
#define LCC_DG_ERR_BUFFER_UNAVAILABLE 0x2040
#define LCC_DG_ERR_OUT_OF_ORDER       0x6000

void lcc_datagram_send_ok(const lcc_node_t *node, uint16_t dst_alias)
{
    lcc_frame_t f;
    f.id  = lcc_can_id(LCC_MTI_DATAGRAM_OK, node->alias);
    f.dlc = 2;
    lcc_frame_build_addressed_hdr(f.data, dst_alias, 0);
    (void)lcc_if_tx(&f);
}

void lcc_datagram_send_rejected(const lcc_node_t *node, uint16_t dst_alias,
                                uint16_t error_code)
{
    lcc_frame_t f;
    f.id  = lcc_can_id(LCC_MTI_DATAGRAM_REJECTED, node->alias);
    f.dlc = 4;
    lcc_frame_build_addressed_hdr(f.data, dst_alias, 0);
    f.data[2] = (uint8_t)(error_code >> 8);
    f.data[3] = (uint8_t)(error_code & 0xFF);
    (void)lcc_if_tx(&f);
}

void lcc_datagram_send(const lcc_node_t *node, uint16_t dst_alias,
                       const uint8_t *payload, uint8_t len)
{
    if (len == 0)
        return;

    if (len <= 8) {
        lcc_frame_t f;
        f.id  = lcc_frame_datagram_id(LCC_FRAME_DATAGRAM_ONE,
                                      dst_alias, node->alias);
        f.dlc = len;
        memcpy(f.data, payload, len);
        (void)lcc_if_tx(&f);
        return;
    }

    /* Multi-frame: first carries up to 8 bytes, then MIDDLE(s) of up to 8,
     * and the last is FINAL. */
    uint8_t offset = 0;
    lcc_frame_t f;

    f.id  = lcc_frame_datagram_id(LCC_FRAME_DATAGRAM_FIRST,
                                  dst_alias, node->alias);
    f.dlc = 8;
    memcpy(f.data, payload, 8);
    (void)lcc_if_tx(&f);
    offset = 8;

    while (offset < len) {
        uint8_t remaining = (uint8_t)(len - offset);
        bool    last      = remaining <= 8;
        uint8_t chunk     = last ? remaining : (uint8_t)8;

        f.id  = lcc_frame_datagram_id(last ? LCC_FRAME_DATAGRAM_FINAL
                                           : LCC_FRAME_DATAGRAM_MIDDLE,
                                      dst_alias, node->alias);
        f.dlc = chunk;
        memcpy(f.data, payload + offset, chunk);
        (void)lcc_if_tx(&f);
        offset = (uint8_t)(offset + chunk);
    }
}

bool lcc_datagram_dispatch(lcc_node_t *node, uint16_t src_alias,
                           const uint8_t *payload, uint8_t len)
{
    if (len < 1)
        return false;

    switch (payload[0]) {
    case LCC_MEMCONFIG_CMD:
        lcc_datagram_send_ok(node, src_alias);
        lcc_memconfig_handle(node, src_alias, payload, len);
        return true;
    default:
        /* Unknown datagram protocol — reject rather than silently drop so
         * the sender's state machine unblocks. */
        lcc_datagram_send_rejected(node, src_alias, LCC_DG_ERR_BUFFER_UNAVAILABLE);
        return false;
    }
}

static void reset_rx(lcc_datagram_rx_t *dg)
{
    dg->active = false;
    dg->len    = 0;
}

void lcc_datagram_handle_frame(lcc_node_t *node, const lcc_frame_t *frame)
{
    uint8_t  ftype = lcc_get_can_frame_type(frame->id);
    uint16_t src   = lcc_get_src(frame->id);
    lcc_datagram_rx_t *dg = &node->dg_rx;

    switch (ftype) {
    case LCC_FRAME_DATAGRAM_ONE:
        lcc_datagram_dispatch(node, src, frame->data, frame->dlc);
        reset_rx(dg);
        break;

    case LCC_FRAME_DATAGRAM_FIRST:
        dg->src_alias = src;
        dg->len       = 0;
        dg->active    = true;
        if (frame->dlc <= LCC_DATAGRAM_MAX) {
            memcpy(dg->buf, frame->data, frame->dlc);
            dg->len = frame->dlc;
        }
        break;

    case LCC_FRAME_DATAGRAM_MIDDLE:
        if (!dg->active || dg->src_alias != src)
            break;
        if ((uint16_t)(dg->len + frame->dlc) > LCC_DATAGRAM_MAX) {
            lcc_datagram_send_rejected(node, src, LCC_DG_ERR_BUFFER_UNAVAILABLE);
            reset_rx(dg);
            break;
        }
        memcpy(dg->buf + dg->len, frame->data, frame->dlc);
        dg->len = (uint8_t)(dg->len + frame->dlc);
        break;

    case LCC_FRAME_DATAGRAM_FINAL:
        if (!dg->active || dg->src_alias != src) {
            /* No matching FIRST — treat as a single-frame datagram so we
             * at least attempt to process it rather than silently drop. */
            lcc_datagram_dispatch(node, src, frame->data, frame->dlc);
            reset_rx(dg);
            break;
        }
        if ((uint16_t)(dg->len + frame->dlc) > LCC_DATAGRAM_MAX) {
            lcc_datagram_send_rejected(node, src, LCC_DG_ERR_BUFFER_UNAVAILABLE);
            reset_rx(dg);
            break;
        }
        memcpy(dg->buf + dg->len, frame->data, frame->dlc);
        dg->len = (uint8_t)(dg->len + frame->dlc);
        lcc_datagram_dispatch(node, src, dg->buf, dg->len);
        reset_rx(dg);
        break;
    }
}
