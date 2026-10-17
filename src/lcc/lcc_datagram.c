#include "lcc/lcc_datagram.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_if.h"
#include "lcc/lcc_memconfig.h"

#include <string.h>

/* Rejection codes follow OpenMRN (Datagram.cxx, DatagramCan.cxx): a
 * permanent error for datagrams that can never be taken, OUT_OF_ORDER
 * (temporary, so the sender resends) for frames that arrive without the
 * rest of their datagram. */

void lcc_datagram_send_ok(const lcc_node_t *node, uint16_t dst_alias)
{
    lcc_datagram_send_ok_flags(node, dst_alias, 0);
}

void lcc_datagram_send_ok_flags(const lcc_node_t *node, uint16_t dst_alias,
                                uint8_t flags)
{
    lcc_frame_t f;
    f.id  = lcc_can_id(LCC_MTI_DATAGRAM_OK, node->alias);
    f.dlc = 2;
    lcc_frame_build_addressed_hdr(f.data, dst_alias, 0);
    if (flags != 0)
        f.data[f.dlc++] = flags;
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
    if (len < 1) {
        lcc_datagram_send_rejected(node, src_alias, LCC_ERR_PERMANENT);
        return false;
    }

    switch (payload[0]) {
    case LCC_MEMCONFIG_CMD: {
        uint8_t  flags;
        uint16_t error = lcc_memconfig_check(node, payload, len, &flags);
        if (error) {
            lcc_datagram_send_rejected(node, src_alias, error);
            return false;
        }
        lcc_datagram_send_ok_flags(node, src_alias, flags);
        lcc_memconfig_handle(node, src_alias, payload, len);
        return true;
    }
    default:
        /* Unknown datagram protocol. Permanent, so the sender gives up
         * instead of retrying. */
        lcc_datagram_send_rejected(node, src_alias, LCC_ERR_UNIMPLEMENTED_CMD);
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
        /* One buffer per node: a FIRST from a second sender takes it over,
         * and the first sender's remaining frames are rejected as out of
         * order so it resends. A FIRST from the same sender means it gave
         * up on its last datagram. Either way nobody is locked out by a
         * sender that vanished mid-datagram. */
        dg->src_alias = src;
        dg->len       = 0;
        dg->active    = true;
        if (frame->dlc <= LCC_DATAGRAM_MAX) {
            memcpy(dg->buf, frame->data, frame->dlc);
            dg->len = frame->dlc;
        }
        break;

    case LCC_FRAME_DATAGRAM_MIDDLE:
        if (!dg->active || dg->src_alias != src) {
            /* The buffer, if busy, belongs to someone else: leave it. */
            lcc_datagram_send_rejected(node, src, LCC_ERR_OUT_OF_ORDER);
            break;
        }
        if ((uint16_t)(dg->len + frame->dlc) > LCC_DATAGRAM_MAX) {
            lcc_datagram_send_rejected(node, src, LCC_ERR_PERMANENT);
            reset_rx(dg);
            break;
        }
        memcpy(dg->buf + dg->len, frame->data, frame->dlc);
        dg->len = (uint8_t)(dg->len + frame->dlc);
        break;

    case LCC_FRAME_DATAGRAM_FINAL:
        if (!dg->active || dg->src_alias != src) {
            /* The head of this datagram is missing, so it can't be
             * processed; the sender resends the whole thing. */
            lcc_datagram_send_rejected(node, src, LCC_ERR_OUT_OF_ORDER);
            break;
        }
        if ((uint16_t)(dg->len + frame->dlc) > LCC_DATAGRAM_MAX) {
            lcc_datagram_send_rejected(node, src, LCC_ERR_PERMANENT);
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
