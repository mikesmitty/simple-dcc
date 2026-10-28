#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "railcom/pom_read.h"

/* RCN-217 codes for the bytes these tests need. */
static const uint8_t encode[64] = {
    0xAC, 0xAA, 0xA9, 0xA5, 0xA3, 0xA6, 0x9C, 0x9A,
    0x99, 0x95, 0x93, 0x96, 0x8E, 0x8D, 0x8B, 0xB1,
    0xB2, 0xB4, 0xB8, 0x74, 0x72, 0x6C, 0x6A, 0x69,
    0x65, 0x63, 0x66, 0x5C, 0x5A, 0x59, 0x55, 0x53,
    0x56, 0x4E, 0x4D, 0x4B, 0x47, 0x71, 0xE8, 0xE4,
    0xE2, 0xD1, 0xC9, 0xC5, 0xD8, 0xD4, 0xD2, 0xCA,
    0xC6, 0xCC, 0x78, 0x17, 0x1B, 0x1D, 0x1E, 0x2E,
    0x36, 0x3A, 0x27, 0x2B, 0x2D, 0x35, 0x39, 0x33,
};
#define ACK  0xF0
#define BUSY 0xE1

static dcc_packet_t read_pkt;

/* A cutout after `packet`, with channel 2 holding a POM answer (value
 * >= 0), the bytes in `raw`, or nothing. */
static railcom_cutout_t cutout(uint32_t packet, int value,
                               const uint8_t *raw, uint8_t raw_len)
{
    railcom_cutout_t c;
    memset(&c, 0, sizeof(c));
    c.packet = packet;
    if (value >= 0) {
        c.ch2[0] = encode[(value >> 6) & 0x03];  /* ID 0 */
        c.ch2[1] = encode[value & 0x3F];
        c.ch2_len = 2;
    } else if (raw) {
        memcpy(c.ch2, raw, raw_len);
        c.ch2_len = raw_len;
    }
    return c;
}

static void start(pom_read_t *r)
{
    packet_build_pom_read_byte(&read_pkt, 3, false, 29, POM_READ_COPIES - 1);
    pom_read_start(r, &read_pkt);
}

static void test_packet_word(void)
{
    pom_read_t r;
    start(&r);
    /* [len 5][addr 3][0xE4][CV 29 - 1]; the checksum lands in word 2. */
    assert(r.packet == 0x0503E41C);
    /* Encoding a copy left the caller's packet alone. */
    assert(read_pkt.len == 4);
    printf("  PASS: packet word\n");
}

static void test_answer_on_first_attempt(void)
{
    pom_read_t r;
    start(&r);
    assert(pom_read_poll(&r, 1000) == POM_READ_SEND);
    assert(pom_read_poll(&r, 1010) == POM_READ_WAIT);

    /* Another decoder's packet, even with a POM datagram, doesn't count. */
    railcom_cutout_t other = cutout(0x0504E41C, 99, NULL, 0);
    assert(pom_read_cutout(&r, &other) == POM_READ_WAIT);

    railcom_cutout_t ours = cutout(r.packet, 0x06, NULL, 0);
    assert(pom_read_cutout(&r, &ours) == POM_READ_DONE);
    assert(r.value == 0x06);
    assert(pom_read_poll(&r, 1020) == POM_READ_DONE);
    printf("  PASS: answer on first attempt\n");
}

static void test_ignores_cutouts_before_start(void)
{
    pom_read_t r;
    start(&r);
    railcom_cutout_t ours = cutout(r.packet, 0x06, NULL, 0);
    assert(pom_read_cutout(&r, &ours) == POM_READ_WAIT);
    printf("  PASS: nothing counts before the first send\n");
}

static void test_retry_on_timeout_then_fail(void)
{
    pom_read_t r;
    start(&r);
    uint32_t t = 5000;
    for (int i = 0; i < POM_READ_ATTEMPTS; i++) {
        assert(pom_read_poll(&r, t) == POM_READ_SEND);
        assert(pom_read_poll(&r, t + POM_READ_ATTEMPT_MS - 1) == POM_READ_WAIT);
        t += POM_READ_ATTEMPT_MS;
    }
    assert(pom_read_poll(&r, t) == POM_READ_FAILED);
    assert(pom_read_poll(&r, t + 1000) == POM_READ_FAILED);
    printf("  PASS: retries on timeout, then fails\n");
}

static void test_early_retry_when_every_copy_answers_without_value(void)
{
    pom_read_t r;
    start(&r);
    assert(pom_read_poll(&r, 0) == POM_READ_SEND);

    const uint8_t acks[] = { ACK, ACK };
    for (int i = 0; i < POM_READ_COPIES; i++) {
        railcom_cutout_t c = cutout(r.packet, -1, acks, sizeof(acks));
        assert(pom_read_cutout(&r, &c) == POM_READ_WAIT);
    }
    /* Long before the attempt would time out. */
    assert(pom_read_poll(&r, 20) == POM_READ_SEND);
    assert(r.attempts == 2);

    /* BUSY counts the same way, and a later answer still wins. */
    const uint8_t busy[] = { BUSY };
    railcom_cutout_t c = cutout(r.packet, -1, busy, sizeof(busy));
    assert(pom_read_cutout(&r, &c) == POM_READ_WAIT);
    assert(pom_read_poll(&r, 30) == POM_READ_WAIT);
    c = cutout(r.packet, 200, NULL, 0);
    assert(pom_read_cutout(&r, &c) == POM_READ_DONE);
    assert(r.value == 200);
    printf("  PASS: early retry when every copy answers without a value\n");
}

static void test_tick_wraparound(void)
{
    pom_read_t r;
    start(&r);
    uint32_t t = 0xFFFFFFF0u;
    assert(pom_read_poll(&r, t) == POM_READ_SEND);
    assert(pom_read_poll(&r, t + 100) == POM_READ_WAIT);
    assert(pom_read_poll(&r, t + POM_READ_ATTEMPT_MS) == POM_READ_SEND);
    printf("  PASS: tick wraparound\n");
}

int main(void)
{
    printf("test_pom_read:\n");
    packet_pool_init();
    test_packet_word();
    test_answer_on_first_attempt();
    test_ignores_cutouts_before_start();
    test_retry_on_timeout_then_fail();
    test_early_retry_when_every_copy_answers_without_value();
    test_tick_wraparound();
    printf("All POM read tests passed.\n");
    return 0;
}
