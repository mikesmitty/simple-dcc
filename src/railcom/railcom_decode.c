#include "railcom/railcom_decode.h"

#define INV  RAILCOM_INV
#define ACK  RAILCOM_ACK
#define NACK RAILCOM_NACK
#define BUSY RAILCOM_BUSY
#define RSV  RAILCOM_RSV

/* RCN-217's 4-of-8 code table, inverted: indexed by the UART byte. All 70
 * bytes with four bits set appear: 64 data codes and 6 special ones.
 * Matches OpenMRN's railcom_decode[] (src/dcc/RailCom.cxx). */
static const uint8_t decode_table[256] = {
    /*      x0    x1    x2    x3    x4    x5    x6    x7    x8    x9    xA    xB    xC    xD    xE    xF */
    /* 0x */  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  ACK,
    /* 1x */  INV,  INV,  INV,  INV,  INV,  INV,  INV, 0x33,  INV,  INV,  INV, 0x34,  INV, 0x35, 0x36,  INV,
    /* 2x */  INV,  INV,  INV,  INV,  INV,  INV,  INV, 0x3A,  INV,  INV,  INV, 0x3B,  INV, 0x3C, 0x37,  INV,
    /* 3x */  INV,  INV,  INV, 0x3F,  INV, 0x3D, 0x38,  INV,  INV, 0x3E, 0x39,  INV, NACK,  INV,  INV,  INV,
    /* 4x */  INV,  INV,  INV,  INV,  INV,  INV,  INV, 0x24,  INV,  INV,  INV, 0x23,  INV, 0x22, 0x21,  INV,
    /* 5x */  INV,  INV,  INV, 0x1F,  INV, 0x1E, 0x20,  INV,  INV, 0x1D, 0x1C,  INV, 0x1B,  INV,  INV,  INV,
    /* 6x */  INV,  INV,  INV, 0x19,  INV, 0x18, 0x1A,  INV,  INV, 0x17, 0x16,  INV, 0x15,  INV,  INV,  INV,
    /* 7x */  INV, 0x25, 0x14,  INV, 0x13,  INV,  INV,  INV, 0x32,  INV,  INV,  INV,  INV,  INV,  INV,  INV,
    /* 8x */  INV,  INV,  INV,  INV,  INV,  INV,  INV,  RSV,  INV,  INV,  INV, 0x0E,  INV, 0x0D, 0x0C,  INV,
    /* 9x */  INV,  INV,  INV, 0x0A,  INV, 0x09, 0x0B,  INV,  INV, 0x08, 0x07,  INV, 0x06,  INV,  INV,  INV,
    /* Ax */  INV,  INV,  INV, 0x04,  INV, 0x03, 0x05,  INV,  INV, 0x02, 0x01,  INV, 0x00,  INV,  INV,  INV,
    /* Bx */  INV, 0x0F, 0x10,  INV, 0x11,  INV,  INV,  INV, 0x12,  INV,  INV,  INV,  INV,  INV,  INV,  INV,
    /* Cx */  INV,  INV,  INV,  RSV,  INV, 0x2B, 0x30,  INV,  INV, 0x2A, 0x2F,  INV, 0x31,  INV,  INV,  INV,
    /* Dx */  INV, 0x29, 0x2E,  INV, 0x2D,  INV,  INV,  INV, 0x2C,  INV,  INV,  INV,  INV,  INV,  INV,  INV,
    /* Ex */  INV, BUSY, 0x28,  INV, 0x27,  INV,  INV,  INV, 0x26,  INV,  INV,  INV,  INV,  INV,  INV,  INV,
    /* Fx */  ACK,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,  INV,
};

uint8_t railcom_decode_byte(uint8_t raw)
{
    return decode_table[raw];
}

bool railcom_decode12(const uint8_t raw[2], uint8_t *id, uint8_t *payload)
{
    uint8_t hi = decode_table[raw[0]];
    uint8_t lo = decode_table[raw[1]];
    if (hi >= 64 || lo >= 64)
        return false;
    *id      = hi >> 2;
    *payload = (uint8_t)(((hi & 0x03) << 6) | lo);
    return true;
}

/* Bytes a datagram takes, by ID; 0 = unknown length. From RCN-217 and
 * OpenMRN's parse_internal(). */
static uint8_t datagram_len(uint8_t id)
{
    switch (id) {
    case RAILCOM_ID_POM:
    case RAILCOM_ID_ADR_HIGH:
    case RAILCOM_ID_ADR_LOW:
        return 2;
    case RAILCOM_ID_EXT:
    case RAILCOM_ID_DYN:
        return 3;
    case 8: case 9: case 10: case 11:   /* XPOM */
        return 6;
    default:
        return 0;
    }
}

railcom_pom_t railcom_parse_pom(const uint8_t *ch2, uint8_t len,
                                uint8_t *value)
{
    if (len == 0)
        return RAILCOM_POM_NONE;

    bool busy = false;
    uint8_t i = 0;
    while (i < len) {
        uint8_t d = decode_table[ch2[i]];
        if (d == RAILCOM_ACK || d == RAILCOM_NACK) {
            i++;
            continue;
        }
        if (d == RAILCOM_BUSY) {
            busy = true;
            i++;
            continue;
        }
        if (d >= 64)
            break;

        uint8_t id = d >> 2;
        uint8_t n  = datagram_len(id);
        if (n == 0 || i + n > len)
            break;
        uint8_t dg_id, payload;
        if (id == RAILCOM_ID_POM && railcom_decode12(&ch2[i], &dg_id, &payload)) {
            *value = payload;
            return RAILCOM_POM_VALUE;
        }
        i = (uint8_t)(i + n);
    }
    return busy ? RAILCOM_POM_BUSY : RAILCOM_POM_OTHER;
}
