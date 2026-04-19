#include "serial/serial.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

#include "lcc/lcc_if.h"
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

        lcc_if_on_rx_byte((uint8_t)ch);
    }
}
