#ifndef RAILCOM_DECODE_H
#define RAILCOM_DECODE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * RailCom (RCN-217) decoding, with no hardware access, so it runs in the
 * host tests.
 *
 * During the cutout after each DCC packet, decoders answer at 250 kbaud by
 * drawing current from the rails. Channel 1 (2 bytes) carries the address
 * broadcast. Channel 2 (up to 6 bytes) belongs to the decoder the packet
 * was addressed to, and carries e.g. its answer to a POM read. Each UART
 * byte is a 4-of-8 code (four bits set) worth 6 data bits or one of a few
 * special symbols. A datagram is a 4-bit ID followed by its payload, so the
 * 12-bit datagrams (ID + 8 bits) take two bytes.
 */

#define RAILCOM_CH1_MAX  4   /* RCN-217 sends 2; room for a late decoder */
#define RAILCOM_CH2_MAX  8   /* RCN-217 sends up to 6 */

/* What one cutout brought back. `packet` is the first word the wavegen PIO
 * pulled for the packet just before the cutout: the byte count (including
 * checksum) in the top byte, then the packet's first three bytes. That is
 * enough to tell which decoder channel 2 came from. */
typedef struct {
    uint32_t packet;
    uint8_t  ch1_len;
    uint8_t  ch2_len;
    uint8_t  ch1[RAILCOM_CH1_MAX];
    uint8_t  ch2[RAILCOM_CH2_MAX];
} railcom_cutout_t;

/* railcom_decode_byte() returns a 6-bit value, or one of these. */
enum {
    RAILCOM_INV  = 0xFF,  /* not a valid 4-of-8 code */
    RAILCOM_ACK  = 0xFE,  /* two code points, 0x0F and 0xF0 */
    RAILCOM_NACK = 0xFD,
    RAILCOM_BUSY = 0xFC,
    RAILCOM_RSV  = 0xFB,  /* reserved: 0x87 and 0xC3 */
};

/* Datagram IDs from mobile decoders. */
enum {
    RAILCOM_ID_POM      = 0,
    RAILCOM_ID_ADR_HIGH = 1,
    RAILCOM_ID_ADR_LOW  = 2,
    RAILCOM_ID_EXT      = 3,
    RAILCOM_ID_DYN      = 7,
};

uint8_t railcom_decode_byte(uint8_t raw);

/* Decode a 12-bit datagram from two UART bytes. False if either byte is
 * not a data code. */
bool railcom_decode12(const uint8_t raw[2], uint8_t *id, uint8_t *payload);

typedef enum {
    RAILCOM_POM_NONE,   /* channel 2 was empty */
    RAILCOM_POM_VALUE,  /* found a POM datagram; *value holds the CV value */
    RAILCOM_POM_BUSY,   /* the decoder said BUSY and gave no value */
    RAILCOM_POM_OTHER,  /* something else: ACKs, other datagrams, garbage */
} railcom_pom_t;

/* Look for the answer to a POM read (or write) in channel 2. ACK and NACK
 * bytes are skipped (some decoders pad with them), as are datagrams of
 * known length; an unknown datagram or an invalid byte ends the search. */
railcom_pom_t railcom_parse_pom(const uint8_t *ch2, uint8_t len,
                                uint8_t *value);

#endif /* RAILCOM_DECODE_H */
