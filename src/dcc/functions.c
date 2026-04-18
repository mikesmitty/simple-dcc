#include "dcc/functions.h"

uint16_t fn_update_group_flags(uint16_t flags, uint16_t fn_number) {
    if      (fn_number <=  4) flags |= FN_GROUP1;
    else if (fn_number <=  8) flags |= FN_GROUP2;
    else if (fn_number <= 12) flags |= FN_GROUP3;
    else if (fn_number <= 20) flags |= FN_GROUP4;
    else if (fn_number <= 28) flags |= FN_GROUP5;
    else if (fn_number <= 36) flags |= FN_GROUP6;
    else if (fn_number <= 44) flags |= FN_GROUP7;
    else if (fn_number <= 52) flags |= FN_GROUP8;
    else if (fn_number <= 60) flags |= FN_GROUP9;
    else if (fn_number <= 68) flags |= FN_GROUP10;
    return flags;
}

// F0-F4: 100D DDDD where D4=F0, D3-D0=F4-F1
uint8_t fn_encode_group1(uint64_t functions) {
    return 0x80 | ((uint8_t)(functions >> 1) & 0x0F) |
           ((uint8_t)(functions & 0x01) << 4);
}

// F5-F8: 1011 DDDD (D3=F8 ... D0=F5)
uint8_t fn_encode_group2(uint64_t functions) {
    return 0xB0 | ((uint8_t)(functions >> 5) & 0x0F);
}

// F9-F12: 1010 DDDD (D3=F12 ... D0=F9)
uint8_t fn_encode_group3(uint64_t functions) {
    return 0xA0 | ((uint8_t)(functions >> 9) & 0x0F);
}

// F13-F20 data byte (D7=F20 ... D0=F13). Command byte is 0xDE.
uint8_t fn_encode_group4(uint64_t functions) {
    return (uint8_t)(functions >> 13);
}

// F21-F28 data byte (D7=F28 ... D0=F21). Command byte is 0xDF.
uint8_t fn_encode_group5(uint64_t functions) {
    return (uint8_t)(functions >> 21);
}

// F29-F36 data byte. Command byte is 0xD8.
uint8_t fn_encode_group6(uint64_t functions) {
    return (uint8_t)(functions >> 29);
}

// F37-F44 data byte. Command byte is 0xD9.
uint8_t fn_encode_group7(uint64_t functions) {
    return (uint8_t)(functions >> 37);
}

// F45-F52 data byte. Command byte is 0xDA.
uint8_t fn_encode_group8(uint64_t functions) {
    return (uint8_t)(functions >> 45);
}

// F53-F60 data byte. Command byte is 0xDB.
uint8_t fn_encode_group9(uint64_t functions) {
    return (uint8_t)(functions >> 53);
}

// F61-F68 data byte. F61-F63 live in functions_lo bits 61-63,
// F64-F68 live in functions_hi bits 0-4. Command byte is 0xDC.
uint8_t fn_encode_group10(uint64_t functions_lo, uint8_t functions_hi) {
    uint8_t low_bits  = (uint8_t)((functions_lo >> 61) & 0x07);
    uint8_t high_bits = (uint8_t)((functions_hi & 0x1F) << 3);
    return low_bits | high_bits;
}
