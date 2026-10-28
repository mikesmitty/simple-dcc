#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "railcom/railcom_decode.h"

/* RCN-217's encode table (6-bit value -> UART byte), kept separate from
 * the decoder's table so each checks the other. */
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

#define ACK_A 0xF0
#define ACK_B 0x0F
#define NACK  0x3C
#define BUSY  0xE1

/* Encode a 12-bit datagram (4-bit ID + 8-bit payload) into two bytes. */
static void enc12(uint8_t id, uint8_t payload, uint8_t *out)
{
    out[0] = encode[((id << 2) | (payload >> 6)) & 0x3F];
    out[1] = encode[payload & 0x3F];
}

static int popcount8(unsigned v)
{
    int n = 0;
    for (; v; v >>= 1)
        n += v & 1;
    return n;
}

static void test_decode_table(void)
{
    for (int v = 0; v < 64; v++)
        assert(railcom_decode_byte(encode[v]) == v);

    assert(railcom_decode_byte(ACK_A) == RAILCOM_ACK);
    assert(railcom_decode_byte(ACK_B) == RAILCOM_ACK);
    assert(railcom_decode_byte(NACK)  == RAILCOM_NACK);
    assert(railcom_decode_byte(BUSY)  == RAILCOM_BUSY);
    assert(railcom_decode_byte(0x87)  == RAILCOM_RSV);
    assert(railcom_decode_byte(0xC3)  == RAILCOM_RSV);

    /* Every byte without exactly four bits set is invalid, and every one
     * with four bits set means something. */
    int valid = 0;
    for (int b = 0; b < 256; b++) {
        uint8_t d = railcom_decode_byte((uint8_t)b);
        if (popcount8((unsigned)b) != 4)
            assert(d == RAILCOM_INV);
        else
            assert(d != RAILCOM_INV);
        if (d != RAILCOM_INV)
            valid++;
    }
    assert(valid == 70);
    printf("  PASS: decode table\n");
}

static void test_decode12(void)
{
    uint8_t raw[2], id, payload;
    enc12(RAILCOM_ID_ADR_LOW, 0x5A, raw);
    assert(railcom_decode12(raw, &id, &payload));
    assert(id == RAILCOM_ID_ADR_LOW);
    assert(payload == 0x5A);

    enc12(RAILCOM_ID_ADR_HIGH, 0xC3, raw);
    assert(railcom_decode12(raw, &id, &payload));
    assert(id == RAILCOM_ID_ADR_HIGH);
    assert(payload == 0xC3);

    raw[1] = ACK_A;
    assert(!railcom_decode12(raw, &id, &payload));
    raw[1] = 0xFF;
    assert(!railcom_decode12(raw, &id, &payload));
    printf("  PASS: 12-bit datagrams\n");
}

static void test_pom_value(void)
{
    uint8_t ch2[6], value = 0;
    assert(railcom_parse_pom(ch2, 0, &value) == RAILCOM_POM_NONE);

    for (int v = 0; v < 256; v++) {
        enc12(RAILCOM_ID_POM, (uint8_t)v, ch2);
        assert(railcom_parse_pom(ch2, 2, &value) == RAILCOM_POM_VALUE);
        assert(value == v);
    }

    /* ESU LokPilot V4 pads the 2-byte answer with four NACKs (OpenMRN). */
    enc12(RAILCOM_ID_POM, 0x81, ch2);
    ch2[2] = ch2[3] = ch2[4] = ch2[5] = NACK;
    assert(railcom_parse_pom(ch2, 6, &value) == RAILCOM_POM_VALUE);
    assert(value == 0x81);
    printf("  PASS: POM value\n");
}

static void test_pom_after_other_data(void)
{
    uint8_t ch2[8], value = 0;

    /* ACKs first, then the answer. */
    ch2[0] = ACK_A;
    ch2[1] = ACK_B;
    enc12(RAILCOM_ID_POM, 3, &ch2[2]);
    assert(railcom_parse_pom(ch2, 4, &value) == RAILCOM_POM_VALUE);
    assert(value == 3);

    /* A 3-byte DYN datagram, then the answer. */
    ch2[0] = encode[(RAILCOM_ID_DYN << 2) | 0];
    ch2[1] = encode[1];
    ch2[2] = encode[2];
    enc12(RAILCOM_ID_POM, 0xFE, &ch2[3]);
    assert(railcom_parse_pom(ch2, 5, &value) == RAILCOM_POM_VALUE);
    assert(value == 0xFE);
    printf("  PASS: POM after other data\n");
}

static void test_pom_no_value(void)
{
    uint8_t ch2[6], value = 0x55;

    ch2[0] = ACK_A;
    ch2[1] = ACK_A;
    assert(railcom_parse_pom(ch2, 2, &value) == RAILCOM_POM_OTHER);

    ch2[0] = BUSY;
    assert(railcom_parse_pom(ch2, 1, &value) == RAILCOM_POM_BUSY);

    /* Garbage before the answer ends the search. */
    ch2[0] = 0xFF;
    enc12(RAILCOM_ID_POM, 7, &ch2[1]);
    assert(railcom_parse_pom(ch2, 3, &value) == RAILCOM_POM_OTHER);

    /* An answer cut short. */
    enc12(RAILCOM_ID_POM, 7, ch2);
    assert(railcom_parse_pom(ch2, 1, &value) == RAILCOM_POM_OTHER);

    /* A second byte that isn't a data code. */
    ch2[1] = ACK_A;
    assert(railcom_parse_pom(ch2, 2, &value) == RAILCOM_POM_OTHER);

    /* An ID of unknown length stops the search. */
    ch2[0] = encode[(13 << 2) | 0];
    ch2[1] = encode[0];
    enc12(RAILCOM_ID_POM, 7, &ch2[2]);
    assert(railcom_parse_pom(ch2, 4, &value) == RAILCOM_POM_OTHER);

    assert(value == 0x55);
    printf("  PASS: POM without a value\n");
}

int main(void)
{
    printf("test_railcom_decode:\n");
    test_decode_table();
    test_decode12();
    test_pom_value();
    test_pom_after_other_data();
    test_pom_no_value();
    printf("All RailCom decode tests passed.\n");
    return 0;
}
