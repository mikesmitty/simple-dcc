#ifndef SVC_CV_H
#define SVC_CV_H

#include <stdbool.h>
#include <stdint.h>

#include "dcc/packet.h"

/*
 * Service Mode CV read/write sequencing (NMRA S-9.2.3, Direct mode),
 * independent of the hardware. A decoder on the programming track can
 * only answer "yes" — a ~6 ms current pulse (ACK) — so every operation is
 * a series of instructions, each sent as a burst and judged by whether
 * the decoder ACKed it. svc_io_t supplies that send-and-listen step.
 *
 * Read:  8 Direct Bit Verifies ("is bit N a 1?") build the byte, then a
 *        Direct Byte Verify confirms it. One full retry if the confirm
 *        fails (a missed or spurious ACK on one bit).
 * Write: Direct Byte Write; if the decoder doesn't ACK the write, a
 *        Direct Byte Verify of the new value decides.
 */

typedef enum {
    SVC_OK = 0,
    SVC_NO_ACK,       /* decoder answered, but never confirmed the value */
    SVC_NO_LOCO,      /* no ACK at all during a read: nothing on the track */
    SVC_SHORT,        /* programming track overload; operation aborted */
    SVC_BUSY,         /* another operation is in progress */
    SVC_UNAVAILABLE,  /* programming track hardware not initialized */
} svc_result_t;

typedef struct {
    /* Send `pkt` as one Service Mode instruction: a burst of identical
     * packets that stops early on ACK, followed by decoder recovery time.
     * Sets *ack. Return SVC_OK, or another result to abort the operation.
     * `pkt` may be modified (checksum appended). */
    svc_result_t (*send)(void *ctx, dcc_packet_t *pkt, bool *ack);
    void *ctx;
} svc_io_t;

/* `cv` is 1-based (1..1024). */
svc_result_t svc_cv_read(const svc_io_t *io, uint16_t cv, uint8_t *value);
svc_result_t svc_cv_write(const svc_io_t *io, uint16_t cv, uint8_t value);

#endif /* SVC_CV_H */
