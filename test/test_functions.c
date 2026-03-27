#include <assert.h>
#include <stdio.h>
#include "dcc/functions.h"

static void test_group_flags(void) {
    uint8_t flags = 0;

    flags = fn_update_group_flags(flags, 0);  // F0 → group1
    assert(flags & FN_GROUP1);

    flags = fn_update_group_flags(flags, 5);  // F5 → group2
    assert(flags & FN_GROUP2);

    flags = fn_update_group_flags(flags, 10); // F10 → group3
    assert(flags & FN_GROUP3);

    flags = fn_update_group_flags(flags, 15); // F15 → group4
    assert(flags & FN_GROUP4);

    flags = fn_update_group_flags(flags, 25); // F25 → group5
    assert(flags & FN_GROUP5);

    printf("  PASS: group flags\n");
}

static void test_encode_group1(void) {
    // F0=1, F1=0, F2=1, F3=0, F4=1 → functions = 0b10101 = 0x15
    // Group1 format: 100D DDDD where D4=F0, D3-D0=F4-F1
    // F0=1 → D4=1, F4-F1=0101 → D3-D0=1010
    // Result: 1001 1010 = 0x9A... wait let me recalculate
    // functions bits: F0=bit0=1, F1=bit1=0, F2=bit2=1, F3=bit3=0, F4=bit4=1
    // fn_encode_group1: 0x80 | ((functions>>1) & 0x0F) | ((functions & 0x01) << 4)
    // (functions>>1) & 0x0F = (0x15>>1) & 0x0F = 0x0A & 0x0F = 0x0A
    // (functions & 0x01) << 4 = 1 << 4 = 0x10
    // Result = 0x80 | 0x0A | 0x10 = 0x9A
    uint32_t functions = 0x15; // F0, F2, F4 on
    uint8_t byte = fn_encode_group1(functions);
    assert(byte == 0x9A);

    // All off
    byte = fn_encode_group1(0);
    assert(byte == 0x80);

    printf("  PASS: encode group1\n");
}

static void test_encode_group2(void) {
    // F5=1, F6=1, F7=0, F8=0 → bits 5-8 = 0b0011 at positions 5,6
    // functions = (1<<5) | (1<<6) = 0x60
    // fn_encode_group2: 0xB0 | ((functions>>5) & 0x0F)
    // (0x60>>5) & 0x0F = 0x03 & 0x0F = 0x03
    // Result = 0xB3
    uint32_t functions = (1u << 5) | (1u << 6);
    uint8_t byte = fn_encode_group2(functions);
    assert(byte == 0xB3);

    printf("  PASS: encode group2\n");
}

static void test_encode_group3(void) {
    // F9=1, F10=0, F11=0, F12=1
    // functions = (1<<9) | (1<<12) = 0x1200
    // fn_encode_group3: 0xA0 | ((functions>>9) & 0x0F)
    // (0x1200>>9) & 0x0F = 0x09 & 0x0F = 0x09
    // Result = 0xA9
    uint32_t functions = (1u << 9) | (1u << 12);
    uint8_t byte = fn_encode_group3(functions);
    assert(byte == 0xA9);

    printf("  PASS: encode group3\n");
}

int main(void) {
    printf("test_functions:\n");
    test_group_flags();
    test_encode_group1();
    test_encode_group2();
    test_encode_group3();
    printf("All function encoding tests passed.\n");
    return 0;
}
