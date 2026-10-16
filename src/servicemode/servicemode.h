#ifndef SERVICEMODE_H
#define SERVICEMODE_H

#include <stdbool.h>
#include <stdint.h>

#include "motor/motor.h"
#include "servicemode/svc_cv.h"

/*
 * Programming track (Service Mode) driver. PIO2 drives the prog-track
 * signal pin and idles on NMRA reset packets; decoder ACKs are read from
 * the motor channel's current sense. Operations run one at a time on
 * task_servicemode: callers submit a request and get a callback, so the
 * LCC task never blocks on the ~1 s a CV read takes.
 *
 * Track power comes on for the first request and stays on between
 * back-to-back requests (a DecoderPro sheet read is hundreds of them),
 * then goes off after an idle second.
 */

typedef enum {
    SVC_OP_READ,
    SVC_OP_WRITE,
} svc_op_t;

/* Completion callback, run on task_servicemode. `value` is the byte read
 * (SVC_OP_READ with SVC_OK) or the byte that was to be written. */
typedef void (*servicemode_done_fn)(svc_result_t result, uint8_t value,
                                    void *ctx);

/* One-time init before the scheduler starts. Must be called after
 * motor_init(prog_motor). Returns false if PIO2 can't be claimed. */
bool servicemode_init(motor_t *prog_motor);

/* Queue a CV operation (`cv` is 1-based). Returns SVC_OK if accepted —
 * `done` is then called exactly once — or SVC_BUSY / SVC_UNAVAILABLE
 * without calling it. */
svc_result_t servicemode_submit(svc_op_t op, uint16_t cv, uint8_t value,
                                servicemode_done_fn done, void *ctx);

/* FreeRTOS task that runs submitted operations. */
void task_servicemode(void *params);

#endif /* SERVICEMODE_H */
