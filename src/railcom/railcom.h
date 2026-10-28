#ifndef RAILCOM_H
#define RAILCOM_H

#include <stdbool.h>
#include <stdint.h>

#include "dcc/dcc.h"
#include "wavegen/wavegen.h"

/*
 * RailCom receiver for the detector shield on the main track
 * (hardware/railcom-detector). The shield turns decoder current during the
 * cutout into 250 kbaud serial on D0 (UART0 RX), gated by the cutout signal
 * on D9, the main channel's brake pin (JP1 in its CUTOUT position).
 *
 * The cutout PIO program interrupts the CPU at the cutout's start, at the
 * end of channel 1 and at the cutout's end. The handler drains the UART at
 * each and tags the cutout with the packet word the wavegen reported for
 * the packet before it. Cutouts that brought anything back go to
 * task_railcom, which runs POM reads and logs the channel 1 address
 * broadcast.
 */

typedef enum {
    RAILCOM_READ_STARTED,
    RAILCOM_READ_BUSY,     /* another read is in flight */
    RAILCOM_READ_OFF,      /* no receiver, or the RailCom cutout is turned off */
} railcom_result_t;

/* One-time init before the scheduler starts, after wavegen_init(). POM
 * reads go out through `dcc`. Returns false if no spare IRQ is left. */
bool railcom_init(wavegen_t *wg, dcc_engine_t *dcc);

/* Completion callback, run on task_railcom. `answered` is false if the
 * decoder never answered; otherwise `value` is the CV's value. */
typedef void (*railcom_pom_done_fn)(bool answered, uint8_t value, void *ctx);

/* Start a POM read of `cv` (1-based) from the decoder at `address`. On
 * RAILCOM_READ_STARTED `done` is called exactly once; otherwise never. */
railcom_result_t railcom_pom_read(uint16_t address, bool is_long, uint16_t cv,
                                  railcom_pom_done_fn done, void *ctx);

/* FreeRTOS task that runs POM reads and watches channel 1. */
void task_railcom(void *params);

#endif /* RAILCOM_H */
