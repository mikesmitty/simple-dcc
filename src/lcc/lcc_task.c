#include "lcc/lcc_task.h"
#include "lcc/lcc_if.h"
#include "lcc/lcc_frame.h"

#include "FreeRTOS.h"
#include "task.h"

void lcc_task_init(void)
{
    lcc_if_init();
    /* M3+ will register the CS node here; M2 runs with an empty node table
     * so the dispatcher has nothing to fan frames out to. */
}

void lcc_task_run(void)
{
    lcc_frame_t frame;

    for (;;) {
        /* 100 ms RX budget matches the alias-allocator tick that lands in
         * M3. Received frames are discarded for now — lcc_if has no node
         * registry yet to dispatch them to. */
        if (lcc_if_rx(&frame, 100)) {
            (void)frame;
        }
        /* TODO(M3): alias_tick(), defer TX flush, etc. */
    }
}
