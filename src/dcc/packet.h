#ifndef PACKET_H
#define PACKET_H

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"  // PACKET_POOL_SIZE

#define MAX_PACKET_DATA   6
#define MAX_ENCODED_WORDS 3

#define BROADCAST_ADDRESS 0xFFFF

typedef enum {
    PRIORITY_BEST_EFFORT = 0,
    PRIORITY_LOW         = 25,
    PRIORITY_NORMAL      = 50,
    PRIORITY_HIGH        = 75,
    PRIORITY_EMERGENCY   = 100,
} dcc_priority_t;

typedef struct {
    uint8_t        data[MAX_PACKET_DATA];
    uint8_t        len;
    uint16_t       address;
    dcc_priority_t priority;
    int8_t         repeats;
    bool           in_use;
} dcc_packet_t;

typedef struct {
    uint32_t words[MAX_ENCODED_WORDS];
    uint8_t  count;
} encoded_packet_t;

void           packet_pool_init(void);
dcc_packet_t  *packet_alloc(void);
void           packet_free(dcc_packet_t *pkt);

void           packet_reset(dcc_packet_t *pkt);
void           packet_fill(dcc_packet_t *pkt, const uint8_t *data, uint8_t len,
                            uint16_t address, dcc_priority_t priority, int8_t repeats);
void           packet_add_byte(dcc_packet_t *pkt, uint8_t b);
bool           packet_is_invalid(const dcc_packet_t *pkt);
encoded_packet_t packet_encode(dcc_packet_t *pkt);

void           packet_make_idle(dcc_packet_t *pkt);

// Build a Programming-on-Main (POM) write-byte packet per NMRA S-9.2.1:
//     {address}  1110_11_VV  CCCCCCCC  DDDDDDDD
// where `VV:CCCCCCCC` is the 10-bit wire encoding of (cv - 1) and
// `DDDDDDDD` is the value. `is_long` forces the 2-byte long-address
// form; short addresses (1..127) with is_long==false emit the 1-byte
// form. Sets PRIORITY_HIGH and the caller-supplied repeat count (NMRA
// recommends ≥3 so the decoder has several chances to decode the
// command). `cv` is 1-based.
void           packet_build_pom_write_byte(dcc_packet_t *pkt,
                                           uint16_t address, bool is_long,
                                           uint16_t cv, uint8_t value,
                                           int8_t repeats);

// Build a POM Verify Byte packet, which RailCom decoders answer with the
// CV's value in channel 2 of the following cutout (RCN-217):
//     {address}  1110_01_VV  CCCCCCCC  00000000
// Same address and CV rules as packet_build_pom_write_byte. Sets
// PRIORITY_HIGH and the caller-supplied repeat count.
void           packet_build_pom_read_byte(dcc_packet_t *pkt,
                                          uint16_t address, bool is_long,
                                          uint16_t cv, int8_t repeats);

// Build a Consist Control packet (S-9.2.1 2.3.1.4) from a CV19 value:
//     {address}  0001_001D  0AAAAAAA
// D is bit 7 of `cv19` (1 = reversed in the consist) and AAAAAAA bits 0-6,
// the consist address. A consist address of 0 takes the loco out of its
// consist and is sent as 0x12 0x00. Same address rules and priority as
// packet_build_pom_write_byte.
void           packet_build_consist_control(dcc_packet_t *pkt,
                                            uint16_t address, bool is_long,
                                            uint8_t cv19, int8_t repeats);

// Build a CV access short form packet (S-9.2.1 2.3.7.2) that writes two
// CVs at once. Decoders act on these only after two identical packets, so
// `repeats` must be at least 1.
//     {address}  1111_0100  CV17  CV18    Long Address (also sets CV29 bit 5)
//     {address}  1111_0110  CV19  CV20    Consist Address
// Same address rules and priority as packet_build_pom_write_byte.
void           packet_build_long_address(dcc_packet_t *pkt,
                                         uint16_t address, bool is_long,
                                         uint8_t cv17, uint8_t cv18,
                                         int8_t repeats);
void           packet_build_long_consist_address(dcc_packet_t *pkt,
                                                 uint16_t address, bool is_long,
                                                 uint8_t cv19, uint8_t cv20,
                                                 int8_t repeats);

// Build an NMRA S-9.2.3 Direct Byte Service Mode packet. Service Mode
// packets have no address byte — they are broadcast on the isolated
// programming track. Wire layout:
//     0111_CC_VV  CCCCCCCC  DDDDDDDD
// where CC = 11 (write) or 01 (verify), VV:CCCCCCCC is the 10-bit wire
// encoding of (cv - 1), and DDDDDDDD is the value byte. `cv` is 1-based
// (1..1024). Priority/repeats are irrelevant: callers feed these directly
// to the dedicated service-mode PIO, not the shared priority queue.
void           packet_build_svc_write_byte(dcc_packet_t *pkt,
                                           uint16_t cv, uint8_t value);
void           packet_build_svc_verify_byte(dcc_packet_t *pkt,
                                            uint16_t cv, uint8_t value);

// Direct Bit Verify (S-9.2.3 bit manipulation, K = 0):
//     0111_10_VV  CCCCCCCC  1110_DBBB
// The decoder ACKs iff bit BBB (0..7) of the CV equals D (`value`).
void           packet_build_svc_verify_bit(dcc_packet_t *pkt, uint16_t cv,
                                           uint8_t bit, bool value);

#endif // PACKET_H
