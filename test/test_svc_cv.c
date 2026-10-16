#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "servicemode/svc_cv.h"

/* ---- fake decoder -------------------------------------------------------
 * Decodes the Service Mode instructions svc_cv sends and ACKs the way an
 * S-9.2.3 decoder would, with knobs for the failure cases. */

typedef struct {
    bool    present;             /* false: empty track, never ACKs */
    bool    acks_writes;         /* ACKs a Direct Byte Write */
    bool    ignores_byte_verify; /* never ACKs a Direct Byte Verify */
    uint8_t cv[1025];            /* 1-based */
    int     drop_ack_at;         /* swallow the ACK on this send (1-based) */
    int     short_at;            /* report a short on this send */
    int     sends;
    int     bit_verifies;
    int     byte_verifies;
    int     writes;
} fake_decoder_t;

static svc_result_t fake_send(void *ctx, dcc_packet_t *pkt, bool *ack) {
    fake_decoder_t *d = ctx;
    d->sends++;
    if (d->short_at == d->sends)
        return SVC_SHORT;

    assert(pkt->len == 3);
    uint8_t  b0 = pkt->data[0], b1 = pkt->data[1], b2 = pkt->data[2];
    uint16_t cv = (uint16_t)((((b0 & 0x03) << 8) | b1) + 1);
    bool a = false;

    switch (b0 & 0xFC) {
    case 0x74:                                   /* verify byte */
        d->byte_verifies++;
        a = !d->ignores_byte_verify && d->cv[cv] == b2;
        break;
    case 0x7C:                                   /* write byte */
        d->writes++;
        if (d->present)
            d->cv[cv] = b2;
        a = d->acks_writes;
        break;
    case 0x78:                                   /* bit manipulation */
        assert((b2 & 0xF0) == 0xE0);             /* K=0: verify only */
        d->bit_verifies++;
        a = ((d->cv[cv] >> (b2 & 0x07)) & 1) == ((b2 >> 3) & 1);
        break;
    default:
        assert(!"unexpected Service Mode instruction");
    }

    if (!d->present || d->drop_ack_at == d->sends)
        a = false;
    *ack = a;
    return SVC_OK;
}

static fake_decoder_t g_dec;
static const svc_io_t g_io = { .send = fake_send, .ctx = &g_dec };

static void reset_decoder(void) {
    memset(&g_dec, 0, sizeof(g_dec));
    g_dec.present     = true;
    g_dec.acks_writes = true;
}

/* ---- reads ------------------------------------------------------------- */

static void test_read_bitwise(void) {
    reset_decoder();
    g_dec.cv[29] = 0xA5;

    uint8_t v = 0;
    assert(svc_cv_read(&g_io, 29, &v) == SVC_OK);
    assert(v == 0xA5);
    assert(g_dec.bit_verifies == 8);
    assert(g_dec.byte_verifies == 1);
    assert(g_dec.sends == 9);
    printf("  PASS: read = 8 bit verifies + 1 byte verify\n");
}

static void test_read_zero_and_ff(void) {
    reset_decoder();
    g_dec.cv[1] = 0x00;
    g_dec.cv[8] = 0xFF;

    uint8_t v = 0x77;
    assert(svc_cv_read(&g_io, 1, &v) == SVC_OK);
    assert(v == 0x00);

    assert(svc_cv_read(&g_io, 8, &v) == SVC_OK);
    assert(v == 0xFF);
    printf("  PASS: read handles 0x00 and 0xFF\n");
}

static void test_read_high_cv(void) {
    reset_decoder();
    g_dec.cv[1024] = 0x3C;
    g_dec.cv[256]  = 0x11;   /* neighbors must not leak in */
    g_dec.cv[768]  = 0x22;

    uint8_t v = 0;
    assert(svc_cv_read(&g_io, 1024, &v) == SVC_OK);
    assert(v == 0x3C);
    printf("  PASS: read CV 1024\n");
}

static void test_read_retries_after_missed_ack(void) {
    reset_decoder();
    g_dec.cv[3] = 0x01;
    g_dec.drop_ack_at = 1;   /* bit 0's ACK lost on the first pass */

    uint8_t v = 0;
    assert(svc_cv_read(&g_io, 3, &v) == SVC_OK);
    assert(v == 0x01);
    assert(g_dec.sends == 18);   /* full second pass */
    printf("  PASS: read retries once when the confirm fails\n");
}

static void test_read_no_decoder(void) {
    reset_decoder();
    g_dec.present = false;

    uint8_t v = 0;
    assert(svc_cv_read(&g_io, 1, &v) == SVC_NO_LOCO);
    assert(g_dec.sends == 18);
    printf("  PASS: read with nothing on the track reports no loco\n");
}

static void test_read_unconfirmed(void) {
    reset_decoder();
    g_dec.cv[5] = 0x42;
    g_dec.ignores_byte_verify = true;

    uint8_t v = 0;
    assert(svc_cv_read(&g_io, 5, &v) == SVC_NO_ACK);
    printf("  PASS: read that never confirms reports no ACK\n");
}

static void test_read_short_aborts(void) {
    reset_decoder();
    g_dec.cv[1] = 0xFF;
    g_dec.short_at = 3;

    uint8_t v = 0;
    assert(svc_cv_read(&g_io, 1, &v) == SVC_SHORT);
    assert(g_dec.sends == 3);
    printf("  PASS: short aborts a read immediately\n");
}

/* ---- writes ------------------------------------------------------------ */

static void test_write_acked(void) {
    reset_decoder();

    assert(svc_cv_write(&g_io, 1, 0x03) == SVC_OK);
    assert(g_dec.cv[1] == 0x03);
    assert(g_dec.writes == 1);
    assert(g_dec.byte_verifies == 0);
    printf("  PASS: write ACKed by the decoder\n");
}

static void test_write_verified_when_not_acked(void) {
    reset_decoder();
    g_dec.acks_writes = false;

    assert(svc_cv_write(&g_io, 29, 0x06) == SVC_OK);
    assert(g_dec.cv[29] == 0x06);
    assert(g_dec.writes == 1);
    assert(g_dec.byte_verifies == 1);
    printf("  PASS: unACKed write confirmed by verify\n");
}

static void test_write_no_decoder(void) {
    reset_decoder();
    g_dec.present = false;

    assert(svc_cv_write(&g_io, 1, 0x03) == SVC_NO_ACK);
    printf("  PASS: write with nothing on the track reports no ACK\n");
}

static void test_write_short_aborts(void) {
    reset_decoder();
    g_dec.short_at = 1;

    assert(svc_cv_write(&g_io, 1, 0x03) == SVC_SHORT);
    assert(g_dec.sends == 1);
    printf("  PASS: short aborts a write\n");
}

int main(void) {
    printf("test_svc_cv:\n");
    test_read_bitwise();
    test_read_zero_and_ff();
    test_read_high_cv();
    test_read_retries_after_missed_ack();
    test_read_no_decoder();
    test_read_unconfirmed();
    test_read_short_aborts();
    test_write_acked();
    test_write_verified_when_not_acked();
    test_write_no_decoder();
    test_write_short_aborts();
    printf("All Service Mode CV tests passed.\n");
    return 0;
}
