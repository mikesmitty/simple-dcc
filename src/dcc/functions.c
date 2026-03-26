#include "dcc/functions.h"

uint8_t fn_update_group_flags(uint8_t flags, uint16_t fn_number) {
    if (fn_number <= 4)       flags |= FN_GROUP1;
    else if (fn_number <= 8)  flags |= FN_GROUP2;
    else if (fn_number <= 12) flags |= FN_GROUP3;
    else if (fn_number <= 20) flags |= FN_GROUP4;
    else                      flags |= FN_GROUP5;
    return flags;
}

// F0-F4: 100D DDDD where D4=F0, D3-D0=F4-F1
uint8_t fn_encode_group1(uint32_t functions) {
    return 0x80 | ((uint8_t)(functions >> 1) & 0x0F) |
           ((uint8_t)(functions & 0x01) << 4);
}

// F5-F8: 1011 DDDD
uint8_t fn_encode_group2(uint32_t functions) {
    return 0xB0 | ((uint8_t)(functions >> 5) & 0x0F);
}

// F9-F12: 1010 DDDD
uint8_t fn_encode_group3(uint32_t functions) {
    return 0xA0 | ((uint8_t)(functions >> 9) & 0x0F);
}
