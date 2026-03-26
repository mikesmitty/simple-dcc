#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dcc/packet.h"

// Minimal heap implementation test (tests the encoding/priority logic,
// not the FreeRTOS queue integration which requires hardware).

static void test_priority_ordering(void) {
    // Verify that higher priority values sort higher
    assert(PRIORITY_EMERGENCY > PRIORITY_HIGH);
    assert(PRIORITY_HIGH > PRIORITY_NORMAL);
    assert(PRIORITY_NORMAL > PRIORITY_LOW);
    assert(PRIORITY_LOW > PRIORITY_BEST_EFFORT);
    printf("  PASS: priority ordering\n");
}

static void test_packet_fill_and_encode(void) {
    packet_pool_init();

    // Test long address encoding: address 1234
    // Long address: 0xC0 | (1234 >> 8) = 0xC0 | 0x04 = 0xC4
    // Low byte: 1234 & 0xFF = 0xD2
    dcc_packet_t *pkt = packet_alloc();
    assert(pkt != NULL);

    // Simulate what new_addressed_packet does for long address
    packet_add_byte(pkt, 0xC0 | (uint8_t)(1234 >> 8)); // 0xC4
    packet_add_byte(pkt, (uint8_t)1234);                 // 0xD2
    // Add 128-step speed command
    packet_add_byte(pkt, 0x3F);
    packet_add_byte(pkt, 0x85); // speed 5 forward

    pkt->address = 1234;
    pkt->priority = PRIORITY_HIGH;

    assert(pkt->len == 4);
    assert(pkt->data[0] == 0xC4);
    assert(pkt->data[1] == 0xD2);

    encoded_packet_t enc = packet_encode(pkt);
    // After checksum: 5 bytes [0xC4, 0xD2, 0x3F, 0x85, checksum]
    // checksum = 0xC4 ^ 0xD2 ^ 0x3F ^ 0x85 = 0xC4^0xD2=0x16, 0x16^0x3F=0x29, 0x29^0x85=0xAC
    // Word 0: [len=5][0xC4][0xD2][0x3F] = 0x05C4D23F
    // Word 1: [0x85][0xAC][0x00][0x00] = 0x85AC0000
    assert(enc.count == 2);
    assert(enc.words[0] == 0x05C4D23F);
    assert(enc.words[1] == 0x85AC0000);

    packet_free(pkt);
    printf("  PASS: long address encode\n");
}

static void test_dedup_comparison(void) {
    packet_pool_init();

    dcc_packet_t *a = packet_alloc();
    dcc_packet_t *b = packet_alloc();

    uint8_t data[] = { 0x03, 0x3F, 0x80 };
    packet_fill(a, data, 3, 3, PRIORITY_NORMAL, 0);
    packet_fill(b, data, 3, 3, PRIORITY_NORMAL, 0);

    // Same data, address, and length — should match for dedup
    assert(a->address == b->address);
    assert(a->len == b->len);
    assert(memcmp(a->data, b->data, a->len) == 0);

    // Different address — should not match
    b->address = 5;
    assert(a->address != b->address);

    packet_free(a);
    packet_free(b);
    printf("  PASS: dedup comparison\n");
}

int main(void) {
    printf("test_priority_queue_logic:\n");
    test_priority_ordering();
    test_packet_fill_and_encode();
    test_dedup_comparison();
    printf("All priority queue logic tests passed.\n");
    return 0;
}
