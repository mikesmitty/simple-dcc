#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dcc/packet.h"

static void test_idle_packet_encode(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();
    assert(pkt != NULL);

    packet_make_idle(pkt);
    assert(pkt->len == 2);
    assert(pkt->data[0] == 0xFF);
    assert(pkt->data[1] == 0x00);

    encoded_packet_t enc = packet_encode(pkt);
    // After checksum: [0xFF, 0x00, 0xFF] = 3 bytes
    // Word 0: [len=3][0xFF][0x00][0xFF] = 0x03FF00FF
    assert(enc.count == 1);
    assert(enc.words[0] == 0x03FF00FF);

    packet_free(pkt);
    printf("  PASS: idle packet encode\n");
}

static void test_short_address_throttle_encode(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();
    assert(pkt != NULL);

    // Short address 3, 128-step speed command: [0x03, 0x3F, 0x85]
    uint8_t data[] = { 0x03, 0x3F, 0x85 };
    packet_fill(pkt, data, 3, 3, PRIORITY_HIGH, 0);

    encoded_packet_t enc = packet_encode(pkt);
    // After checksum: [0x03, 0x3F, 0x85, 0xB9] = 4 bytes
    // Word 0: [len=4][0x03][0x3F][0x85] = 0x04033F85
    // Word 1: [0xB9][0x00][0x00][0x00] = 0xB9000000
    assert(enc.count == 2);
    assert(enc.words[0] == 0x04033F85);
    assert(enc.words[1] == 0xB9000000);

    packet_free(pkt);
    printf("  PASS: short address throttle encode\n");
}

static void test_pool_alloc_free(void) {
    packet_pool_init();

    // Allocate all packets
    dcc_packet_t *pkts[PACKET_POOL_SIZE];
    for (int i = 0; i < PACKET_POOL_SIZE; i++) {
        pkts[i] = packet_alloc();
        assert(pkts[i] != NULL);
    }

    // Pool should be exhausted
    assert(packet_alloc() == NULL);

    // Free one and reallocate
    packet_free(pkts[0]);
    dcc_packet_t *realloc = packet_alloc();
    assert(realloc != NULL);
    assert(realloc == pkts[0]);

    printf("  PASS: pool alloc/free\n");
}

static void test_packet_is_invalid(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();

    // Empty packet is invalid
    assert(packet_is_invalid(pkt));

    // Add a zero byte — still invalid (first byte == 0)
    packet_add_byte(pkt, 0x00);
    assert(packet_is_invalid(pkt));

    // Reset and add valid byte
    packet_reset(pkt);
    packet_add_byte(pkt, 0xFF);
    assert(!packet_is_invalid(pkt));

    packet_free(pkt);
    printf("  PASS: packet_is_invalid\n");
}

static void test_checksum(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();

    // Idle packet: 0xFF ^ 0x00 = 0xFF
    packet_make_idle(pkt);
    encoded_packet_t enc = packet_encode(pkt);
    // Verify the encoded word contains the checksum
    // 0x03FF00FF: byte 2 = 0x00, byte 3 = 0xFF (checksum)
    assert(enc.words[0] == 0x03FF00FF);

    packet_free(pkt);
    printf("  PASS: checksum\n");
}

int main(void) {
    printf("test_packet:\n");
    test_idle_packet_encode();
    test_short_address_throttle_encode();
    test_pool_alloc_free();
    test_packet_is_invalid();
    test_checksum();
    printf("All packet tests passed.\n");
    return 0;
}
