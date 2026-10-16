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

    // Broadcast-address packet (data[0] == 0x00) is valid — used by
    // dcc_emergency_stop_all for the all-loco estop broadcast.
    packet_add_byte(pkt, 0x00);
    assert(!packet_is_invalid(pkt));

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

static void test_pom_write_short_address(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();

    // Address 3 (short), CV 3, value 42.
    // Wire: [0x03, 0xEC, 0x02, 0x2A]
    //   0x03 = short address
    //   0xEC = 1110_1100 (write-byte opcode, cv high bits = 00)
    //   0x02 = cv - 1 low byte (cv=3 → 2)
    //   0x2A = 42
    packet_build_pom_write_byte(pkt, 3, false, 3, 42, 3);
    assert(pkt->len == 4);
    assert(pkt->data[0] == 0x03);
    assert(pkt->data[1] == 0xEC);
    assert(pkt->data[2] == 0x02);
    assert(pkt->data[3] == 0x2A);
    assert(pkt->address == 3);
    assert(pkt->priority == PRIORITY_HIGH);
    assert(pkt->repeats == 3);

    packet_free(pkt);
    printf("  PASS: POM write, short address\n");
}

static void test_pom_write_long_address(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();

    // Address 3000 (long), CV 29, value 0x06.
    //   0xCB = 11_001011 (long prefix | (3000 >> 8) = 0x0B)
    //   0xB8 = 3000 & 0xFF
    //   0xEC = write-byte, cv high = 0
    //   0x1C = cv - 1 = 28 low byte
    //   0x06 = value
    packet_build_pom_write_byte(pkt, 3000, true, 29, 0x06, 3);
    assert(pkt->len == 5);
    assert(pkt->data[0] == 0xCB);
    assert(pkt->data[1] == 0xB8);
    assert(pkt->data[2] == 0xEC);
    assert(pkt->data[3] == 0x1C);
    assert(pkt->data[4] == 0x06);

    packet_free(pkt);
    printf("  PASS: POM write, long address\n");
}

static void test_pom_write_high_cv_address(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();

    // Address 3 (short), CV 1024 (max), value 5.
    //   wire_cv = 1023 = 0x3FF → high 2 bits = 0x03, low 8 = 0xFF
    //   first POM byte = 0xEC | 0x03 = 0xEF
    packet_build_pom_write_byte(pkt, 3, false, 1024, 5, 3);
    assert(pkt->len == 4);
    assert(pkt->data[0] == 0x03);
    assert(pkt->data[1] == 0xEF);
    assert(pkt->data[2] == 0xFF);
    assert(pkt->data[3] == 0x05);

    packet_free(pkt);
    printf("  PASS: POM write, high CV (1024)\n");
}

static void test_pom_write_auto_long_address(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();

    // Address 200 with is_long=false → builder must auto-promote to
    // long form since 200 > SHORT_ADDRESS_MAX (127). This matches the
    // throttle-path fix aecb096.
    packet_build_pom_write_byte(pkt, 200, false, 3, 42, 3);
    assert(pkt->len == 5);
    assert(pkt->data[0] == 0xC0);     // 11_000000 | (200 >> 8) = 0xC0
    assert(pkt->data[1] == 0xC8);     // 200 & 0xFF

    packet_free(pkt);
    printf("  PASS: POM write, auto-promote > 127 to long\n");
}

static void test_pom_write_short_forced_long(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();

    // Address 3 forced long — some decoders are configured with long
    // addresses in the short range. Must emit the 2-byte long form.
    packet_build_pom_write_byte(pkt, 3, true, 3, 42, 3);
    assert(pkt->len == 5);
    assert(pkt->data[0] == 0xC0);     // 11_000000 | 0
    assert(pkt->data[1] == 0x03);

    packet_free(pkt);
    printf("  PASS: POM write, short-in-long-form honored\n");
}

static void test_svc_write_byte_layout(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();

    // Service Mode Direct Byte Write, CV 3 = 0x42. No address byte.
    //   0x7C = 0111_1100 (write-byte opcode, cv high bits = 00)
    //   0x02 = cv - 1 low byte
    //   0x42 = value
    packet_build_svc_write_byte(pkt, 3, 0x42);
    assert(pkt->len == 3);
    assert(pkt->data[0] == 0x7C);
    assert(pkt->data[1] == 0x02);
    assert(pkt->data[2] == 0x42);
    assert(pkt->address == BROADCAST_ADDRESS);

    // XOR checksum emerges after encode: 0x7C ^ 0x02 ^ 0x42 = 0x3C
    encoded_packet_t enc = packet_encode(pkt);
    assert(enc.count == 2);
    // Word 0: [len=4][0x7C][0x02][0x42]
    assert(enc.words[0] == 0x047C0242u);
    // Word 1: [0x3C][...]
    assert((enc.words[1] >> 24) == 0x3C);

    packet_free(pkt);
    printf("  PASS: SVC write, byte layout\n");
}

static void test_svc_verify_byte_layout(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();

    // Service Mode Direct Byte Verify, CV 1 = 3 (factory-default address check).
    //   0x74 = 0111_0100 (verify-byte opcode, cv high bits = 00)
    //   0x00 = cv - 1 low byte
    //   0x03 = expected value
    packet_build_svc_verify_byte(pkt, 1, 3);
    assert(pkt->len == 3);
    assert(pkt->data[0] == 0x74);
    assert(pkt->data[1] == 0x00);
    assert(pkt->data[2] == 0x03);

    packet_free(pkt);
    printf("  PASS: SVC verify, byte layout\n");
}

static void test_svc_high_cv(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();

    // CV 1024 → wire_cv = 1023 = 0x3FF.
    //   opcode byte = 0x7C | 0x03 = 0x7F
    //   low byte    = 0xFF
    packet_build_svc_write_byte(pkt, 1024, 0x55);
    assert(pkt->data[0] == 0x7F);
    assert(pkt->data[1] == 0xFF);
    assert(pkt->data[2] == 0x55);

    packet_free(pkt);
    printf("  PASS: SVC write, CV 1024\n");
}

static void test_svc_verify_bit_layout(void) {
    packet_pool_init();
    dcc_packet_t *pkt = packet_alloc();

    // Direct Bit Verify, CV 29 bit 5 == 1.
    //   0x78 = 0111_1000 (bit-manipulation opcode, cv high bits = 00)
    //   0x1C = cv - 1 low byte
    //   0xED = 1110_1101 (K=0 verify, D=1, BBB=101)
    packet_build_svc_verify_bit(pkt, 29, 5, true);
    assert(pkt->len == 3);
    assert(pkt->data[0] == 0x78);
    assert(pkt->data[1] == 0x1C);
    assert(pkt->data[2] == 0xED);
    assert(pkt->address == BROADCAST_ADDRESS);

    // D=0, bit 0, CV 1024 (cv high bits = 11).
    packet_build_svc_verify_bit(pkt, 1024, 0, false);
    assert(pkt->data[0] == 0x7B);
    assert(pkt->data[1] == 0xFF);
    assert(pkt->data[2] == 0xE0);

    packet_free(pkt);
    printf("  PASS: SVC verify, bit layout\n");
}

int main(void) {
    printf("test_packet:\n");
    test_idle_packet_encode();
    test_short_address_throttle_encode();
    test_pool_alloc_free();
    test_packet_is_invalid();
    test_checksum();
    test_pom_write_short_address();
    test_pom_write_long_address();
    test_pom_write_high_cv_address();
    test_pom_write_auto_long_address();
    test_pom_write_short_forced_long();
    test_svc_write_byte_layout();
    test_svc_verify_byte_layout();
    test_svc_high_cv();
    test_svc_verify_bit_layout();
    printf("All packet tests passed.\n");
    return 0;
}
