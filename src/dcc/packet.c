#include "dcc/packet.h"
#include <string.h>

static dcc_packet_t packet_pool[PACKET_POOL_SIZE];

void packet_pool_init(void) {
    memset(packet_pool, 0, sizeof(packet_pool));
}

dcc_packet_t *packet_alloc(void) {
    for (int i = 0; i < PACKET_POOL_SIZE; i++) {
        if (!packet_pool[i].in_use) {
            packet_pool[i].in_use = true;
            packet_pool[i].len = 0;
            packet_pool[i].address = 0;
            packet_pool[i].priority = PRIORITY_NORMAL;
            packet_pool[i].repeats = 0;
            return &packet_pool[i];
        }
    }
    return NULL;
}

void packet_free(dcc_packet_t *pkt) {
    if (pkt) {
        pkt->in_use = false;
    }
}

void packet_reset(dcc_packet_t *pkt) {
    pkt->len = 0;
    pkt->address = 0;
    pkt->priority = PRIORITY_NORMAL;
    pkt->repeats = 0;
}

void packet_fill(dcc_packet_t *pkt, const uint8_t *data, uint8_t len,
                 uint16_t address, dcc_priority_t priority, int8_t repeats) {
    uint8_t max = MAX_PACKET_DATA - 1; // reserve 1 for checksum
    if (len > max) {
        len = max;
    }
    memcpy(pkt->data, data, len);
    pkt->len = len;
    pkt->address = address;
    pkt->priority = priority;
    pkt->repeats = repeats;
}

void packet_add_byte(dcc_packet_t *pkt, uint8_t b) {
    if (pkt->len < MAX_PACKET_DATA - 1) {
        pkt->data[pkt->len++] = b;
    }
}

bool packet_is_invalid(const dcc_packet_t *pkt) {
    return (pkt->len == 0 || pkt->data[0] == 0);
}

static void packet_add_checksum(dcc_packet_t *pkt) {
    if (pkt->len == 0) return;
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < pkt->len; i++) {
        checksum ^= pkt->data[i];
    }
    pkt->data[pkt->len++] = checksum;
}

// Encode packs the packet into the PIO wavegen's uint32 word format.
//
// Word layout: each 32-bit word holds up to 4 bytes, MSB first. The first word
// reserves its high byte for the total byte count (including XOR checksum),
// leaving room for 3 data bytes. Subsequent words carry 4 data bytes each.
//
// Example: idle packet [0xFF, 0x00] + checksum 0xFF = 3 bytes:
//   word 0: 0x03_FF_00_FF  =  [len=3][0xFF][0x00][0xFF]
//
// Partial final words MUST be flushed or the PIO desyncs.
encoded_packet_t packet_encode(dcc_packet_t *pkt) {
    encoded_packet_t enc = { .count = 0 };
    if (pkt->len == 0) return enc;

    packet_add_checksum(pkt);

    uint32_t word = (uint32_t)pkt->len << 24;
    int shift = 2; // first word packs 3 bytes (positions 2,1,0)

    for (uint8_t i = 0; i < pkt->len; i++) {
        word |= (uint32_t)pkt->data[i] << (8 * shift);
        shift--;
        if (shift < 0) {
            enc.words[enc.count++] = word;
            word = 0;
            shift = 3;
        }
    }
    // Flush partial final word
    if (shift < 3) {
        enc.words[enc.count++] = word;
    }
    return enc;
}

void packet_make_idle(dcc_packet_t *pkt) {
    const uint8_t idle[] = { 0xFF, 0x00 };
    packet_fill(pkt, idle, 2, BROADCAST_ADDRESS, PRIORITY_BEST_EFFORT, 0);
}
