#ifndef POM_READ_H
#define POM_READ_H

#include <stdbool.h>
#include <stdint.h>

#include "dcc/packet.h"
#include "railcom/railcom_decode.h"

/*
 * Reading a CV on the main (POM read) over RailCom, independent of the
 * hardware. The command station sends a POM Verify Byte to the decoder's
 * address; the decoder answers with the CV value in channel 2 of the
 * cutout that follows (RCN-217 ID 0). The answer carries no address, so it
 * only counts when that cutout followed one of our packets: the cutout's
 * packet word must match the read packet's first word.
 *
 * Each attempt sends POM_READ_COPIES back-to-back copies (as OpenMRN does)
 * and waits up to POM_READ_ATTEMPT_MS for an answer. It gives up early once
 * every copy has come back with channel 2 data but no value (ACK, BUSY,
 * garbage). After POM_READ_ATTEMPTS attempts the read fails.
 */

#define POM_READ_COPIES      2
#define POM_READ_ATTEMPT_MS  250
#define POM_READ_ATTEMPTS    4

typedef enum {
    POM_READ_WAIT,    /* nothing to do yet */
    POM_READ_SEND,    /* send the read packet (again) */
    POM_READ_DONE,    /* the decoder answered; the value is in `value` */
    POM_READ_FAILED,  /* no answer after every attempt */
} pom_read_step_t;

typedef struct {
    uint32_t packet;     /* first wavegen word of the read packet */
    uint32_t sent_ms;    /* when the current attempt started */
    uint8_t  attempts;   /* attempts started so far */
    uint8_t  answers;    /* matching cutouts this attempt, none with a value */
    bool     done;
    uint8_t  value;
} pom_read_t;

/* Start a read. `pkt` is the read packet, as built by
 * packet_build_pom_read_byte(); it is not modified. */
void pom_read_start(pom_read_t *r, const dcc_packet_t *pkt);

/* Feed one cutout. Returns POM_READ_DONE once the decoder has answered,
 * otherwise POM_READ_WAIT. */
pom_read_step_t pom_read_cutout(pom_read_t *r, const railcom_cutout_t *c);

/* Call right after pom_read_start() and then periodically. Returns
 * POM_READ_SEND when an attempt should start (the first one included),
 * POM_READ_FAILED when they have all run out, else POM_READ_WAIT (or
 * POM_READ_DONE if an answer already came in). */
pom_read_step_t pom_read_poll(pom_read_t *r, uint32_t now_ms);

#endif /* POM_READ_H */
