#include "lcc/lcc_task.h"
#include "lcc/lcc_if.h"
#include "lcc/lcc_frame.h"
#include "lcc/lcc_node.h"
#include "lcc/lcc_alias.h"
#include "lcc/lcc_events.h"

#include "FreeRTOS.h"
#include "task.h"

#define TICK_PERIOD_MS  100

void lcc_task_init(void)
{
    lcc_if_init();
    lcc_node_registry_reset();
    lcc_events_reset();
    /* The CS node (and later train nodes) are registered by the glue
     * layer in src/protocol/lcc_interface.c so the LCC core stays free
     * of DCC-specific config. */
}

static void alias_tick_all(void)
{
    int n = lcc_node_count();
    for (int i = 0; i < n; i++) {
        lcc_node_t *node = lcc_node_at(i);
        if (node && node->alias_state != LCC_A_CONFIRMED)
            lcc_alias_tick(node);
    }
}

void lcc_task_run(void)
{
    TickType_t next_tick = xTaskGetTickCount();
    lcc_frame_t frame;

    for (;;) {
        /* Wait for either an RX frame or the next alias-tick deadline,
         * whichever arrives first. */
        TickType_t now = xTaskGetTickCount();
        TickType_t wait_ms = 0;
        if ((int32_t)(next_tick - now) > 0)
            wait_ms = (TickType_t)((next_tick - now) * portTICK_PERIOD_MS);

        if (lcc_if_rx(&frame, wait_ms))
            lcc_if_dispatch(&frame);

        now = xTaskGetTickCount();
        if ((int32_t)(now - next_tick) >= 0) {
            alias_tick_all();
            next_tick += pdMS_TO_TICKS(TICK_PERIOD_MS);
        }
    }
}
