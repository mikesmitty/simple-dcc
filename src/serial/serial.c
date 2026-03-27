#include "serial/serial.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

#include "protocol/lcc_interface.h"
#include "openlcb/openlcb_gridconnect.h"
#include "drivers/canbus/can_types.h"
#include "drivers/canbus/can_rx_statemachine.h"
#include "util/dbg.h"

void serial_init(void) {
    // stdio_init_all() is called in main
}

void serial_write(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        putchar_raw(data[i]);
    }
}

bool serial_write_ready(void) {
    return stdio_usb_connected();
}

void task_serial(void *params) {
    (void)params;
    gridconnect_buffer_t gc_buf;

    for (;;) {
        // Service USB stack — stdio_flush() calls tud_task() via the
        // mutex-protected stdio_usb_out_flush path.  Needed because
        // stdio_usb_in_chars skips tud_task() when USB is not yet
        // connected, so the background IRQ is the only other caller.
        stdio_flush();
        int ch = getchar_timeout_us(1000); // 1ms timeout for responsive USB
        if (ch == PICO_ERROR_TIMEOUT || ch < 0) {
            continue;
        }

        if (OpenLcbGridConnect_copy_out_gridconnect_when_done((uint8_t)ch, &gc_buf)) {
            DBG("[RX] %s\n", (char *)gc_buf);
            can_msg_t can_msg = {0};
            OpenLcbGridConnect_to_can_msg(&gc_buf, &can_msg);
            lcc_interface_on_rx_can_msg(&can_msg);
            CanRxStatemachine_incoming_can_driver_callback(&can_msg);
        }
    }
}
