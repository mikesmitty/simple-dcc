#include "servicemode/svc_cv.h"

#define READ_ATTEMPTS  2

static svc_result_t send(const svc_io_t *io, dcc_packet_t *pkt, bool *ack)
{
    *ack = false;
    return io->send(io->ctx, pkt, ack);
}

static svc_result_t verify_byte(const svc_io_t *io, uint16_t cv,
                                uint8_t value, bool *ack)
{
    dcc_packet_t pkt = { .in_use = true };
    packet_build_svc_verify_byte(&pkt, cv, value);
    return send(io, &pkt, ack);
}

svc_result_t svc_cv_read(const svc_io_t *io, uint16_t cv, uint8_t *value)
{
    bool any_ack = false;

    for (int attempt = 0; attempt < READ_ATTEMPTS; attempt++) {
        uint8_t v = 0;
        for (uint8_t bit = 0; bit < 8; bit++) {
            dcc_packet_t pkt = { .in_use = true };
            packet_build_svc_verify_bit(&pkt, cv, bit, true);
            bool ack;
            svc_result_t r = send(io, &pkt, &ack);
            if (r != SVC_OK)
                return r;
            if (ack) {
                v |= (uint8_t)(1u << bit);
                any_ack = true;
            }
        }

        bool ack;
        svc_result_t r = verify_byte(io, cv, v, &ack);
        if (r != SVC_OK)
            return r;
        if (ack) {
            *value = v;
            return SVC_OK;
        }
    }

    /* A CV of 0 gets no bit ACKs but its byte verify ACKs, so reaching
     * here with no ACK at all means nothing answered. */
    return any_ack ? SVC_NO_ACK : SVC_NO_LOCO;
}

svc_result_t svc_cv_write(const svc_io_t *io, uint16_t cv, uint8_t value)
{
    dcc_packet_t pkt = { .in_use = true };
    packet_build_svc_write_byte(&pkt, cv, value);
    bool ack;
    svc_result_t r = send(io, &pkt, &ack);
    if (r != SVC_OK)
        return r;
    if (ack)
        return SVC_OK;

    /* Some decoders don't ACK the write itself; the value is what counts. */
    r = verify_byte(io, cv, value, &ack);
    if (r != SVC_OK)
        return r;
    return ack ? SVC_OK : SVC_NO_ACK;
}
