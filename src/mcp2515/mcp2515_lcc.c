#include "mcp2515/mcp2515_lcc.h"
#include "lcc/lcc_if.h"
#include "lcc/lcc_frame.h"
#include "util/dbg.h"

#include "FreeRTOS.h"
#include "task.h"

#define MCP2515_RX_TASK_STACK 512
#define MCP2515_RX_TASK_PRIO  2   /* same as task_protocol — these feed it */

static TaskHandle_t g_rx_task_handle;

/* Egress: LCC → CAN. lcc_if_tx() fans out to every registered transport,
 * so USB and CAN both carry outbound frames. Return true even on TX-
 * buffer-busy — losing a frame on a saturated bus is preferable to
 * stalling the whole TX fan-out. */
static bool mcp2515_egress(const lcc_frame_t *frame, void *ctx)
{
    (void)ctx;
    mcp2515_frame_t out = {
        .id  = frame->id,
        .dlc = frame->dlc,
    };
    for (uint8_t i = 0; i < frame->dlc && i < 8; i++)
        out.data[i] = frame->data[i];
    (void)mcp2515_send(&out);
    return true;
}

/* RX task: blocks on the driver's task notification (raised from the
 * INT-pin ISR), then drains as many frames as the chip holds. */
static void task_mcp2515_rx(void *params)
{
    (void)params;
    for (;;) {
        /* Block indefinitely for the next notification. A 100 ms timeout
         * doubles as a "did we miss an edge" safety net — if we ever
         * drop an interrupt, this still picks up whatever RX buffer the
         * chip has latched. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        mcp2515_frame_t in;
        while (mcp2515_try_receive(&in)) {
            lcc_frame_t f = {
                .id  = in.id,
                .dlc = in.dlc,
            };
            for (uint8_t i = 0; i < in.dlc && i < 8; i++)
                f.data[i] = in.data[i];
            lcc_if_enqueue_frame(&f);
        }
    }
}

bool mcp2515_lcc_start(const mcp2515_config_t *cfg)
{
    if (!cfg) return false;

    if (!mcp2515_init(cfg)) {
        DBG("[MCP2515] init failed\n");
        return false;
    }

    if (!lcc_if_register_transport(mcp2515_egress, NULL)) {
        DBG("[MCP2515] no egress slot free\n");
        return false;
    }

    BaseType_t ok = xTaskCreate(task_mcp2515_rx, "mcp2515_rx",
                                MCP2515_RX_TASK_STACK, NULL,
                                MCP2515_RX_TASK_PRIO, &g_rx_task_handle);
    if (ok != pdPASS) return false;

    mcp2515_set_rx_task(g_rx_task_handle);
    return true;
}
