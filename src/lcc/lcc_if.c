#include "lcc/lcc_if.h"
#include "lcc/lcc_gc.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_node.h"
#include "serial/serial.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#include <string.h>

static QueueHandle_t      rx_q;
static SemaphoreHandle_t  tx_mtx;
static SemaphoreHandle_t  alias_mtx;
static lcc_gc_parser_t    gc_parser;

void lcc_if_init(void)
{
    rx_q      = xQueueCreate(LCC_IF_RX_Q_LEN, sizeof(lcc_frame_t));
    tx_mtx    = xSemaphoreCreateMutex();
    alias_mtx = xSemaphoreCreateMutex();
    memset(&gc_parser, 0, sizeof(gc_parser));
}

void lcc_if_on_rx_byte(uint8_t ch)
{
    lcc_frame_t frame;
    if (lcc_gc_parse_byte(&gc_parser, (char)ch, &frame)) {
        /* Drop on overflow — the bus is noisy; losing an un-consumed frame
         * is preferable to blocking task_serial. */
        (void)xQueueSend(rx_q, &frame, 0);
    }
}

bool lcc_if_rx(lcc_frame_t *out, uint32_t timeout_ms)
{
    return xQueueReceive(rx_q, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool lcc_if_tx(const lcc_frame_t *frame)
{
    if (!serial_write_ready()) {
        /* USB not connected: drop silently so the stack never stalls on
         * a missing host. */
        return true;
    }

    char buf[LCC_GC_MAX_LEN];
    int n = lcc_gc_encode(frame, buf, sizeof(buf));
    if (n <= 0)
        return false;

    xSemaphoreTake(tx_mtx, portMAX_DELAY);
    serial_write((const uint8_t *)buf, (uint16_t)n);
    xSemaphoreGive(tx_mtx);
    return true;
}

bool lcc_if_alias_try_lock(void)
{
    return xSemaphoreTake(alias_mtx, 0) == pdTRUE;
}

void lcc_if_alias_unlock(void)
{
    xSemaphoreGive(alias_mtx);
}

void lcc_if_dispatch(const lcc_frame_t *frame)
{
    int count = lcc_node_count();

    if (lcc_is_control_frame(frame->id)) {
        /* Control frames are bus-wide: collision detection + AME rely on
         * every node seeing them. */
        for (int i = 0; i < count; i++)
            lcc_node_handle_frame(lcc_node_at(i), frame);
        return;
    }

    uint8_t ftype = lcc_get_can_frame_type(frame->id);

    if (ftype == LCC_FRAME_GLOBAL_ADDRESSED) {
        uint16_t mti = lcc_get_mti(frame->id);
        if (mti & LCC_MTI_ADDRESS_MASK) {
            /* Addressed — lookup dst alias, deliver to owner only. */
            if (frame->dlc < 2)
                return;
            uint16_t dst = lcc_frame_addressed_dst(frame);
            lcc_node_t *n = lcc_node_find_by_alias(dst);
            if (n)
                lcc_node_handle_frame(n, frame);
        } else {
            /* Global — deliver to every confirmed node. */
            for (int i = 0; i < count; i++)
                lcc_node_handle_frame(lcc_node_at(i), frame);
        }
        return;
    }

    /* Datagram frames: dst alias is in the top 12 bits of the CAN ID. */
    if (ftype >= LCC_FRAME_DATAGRAM_ONE && ftype <= LCC_FRAME_DATAGRAM_FINAL) {
        uint16_t dst = lcc_get_datagram_dst(frame->id);
        lcc_node_t *n = lcc_node_find_by_alias(dst);
        if (n)
            lcc_node_handle_frame(n, frame);
    }
}
