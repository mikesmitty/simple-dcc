#include "lcc/lcc_if.h"
#include "lcc/lcc_gc.h"
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

void lcc_if_alias_lock(void)
{
    xSemaphoreTake(alias_mtx, portMAX_DELAY);
}

void lcc_if_alias_unlock(void)
{
    xSemaphoreGive(alias_mtx);
}
