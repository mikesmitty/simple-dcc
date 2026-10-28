#include "railcom/pom_read.h"

#include <string.h>

void pom_read_start(pom_read_t *r, const dcc_packet_t *pkt)
{
    memset(r, 0, sizeof(*r));
    /* packet_encode() appends the checksum, so encode a copy. */
    dcc_packet_t copy = *pkt;
    encoded_packet_t enc = packet_encode(&copy);
    r->packet = enc.count ? enc.words[0] : 0;
}

pom_read_step_t pom_read_cutout(pom_read_t *r, const railcom_cutout_t *c)
{
    if (r->done)
        return POM_READ_DONE;
    if (r->attempts == 0 || c->packet != r->packet)
        return POM_READ_WAIT;

    uint8_t value;
    if (railcom_parse_pom(c->ch2, c->ch2_len, &value) == RAILCOM_POM_VALUE) {
        r->done  = true;
        r->value = value;
        return POM_READ_DONE;
    }
    if (r->answers < POM_READ_COPIES)
        r->answers++;
    return POM_READ_WAIT;
}

pom_read_step_t pom_read_poll(pom_read_t *r, uint32_t now_ms)
{
    if (r->done)
        return POM_READ_DONE;

    bool next = r->attempts == 0
             || r->answers >= POM_READ_COPIES
             || now_ms - r->sent_ms >= POM_READ_ATTEMPT_MS;
    if (!next)
        return POM_READ_WAIT;
    if (r->attempts >= POM_READ_ATTEMPTS)
        return POM_READ_FAILED;

    r->attempts++;
    r->answers = 0;
    r->sent_ms = now_ms;
    return POM_READ_SEND;
}
