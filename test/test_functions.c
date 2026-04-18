#include <assert.h>
#include <stdio.h>
#include "dcc/functions.h"

static void test_group_flags(void) {
    uint16_t flags = 0;

    flags = fn_update_group_flags(flags, 0);   // F0 → group1
    assert(flags & FN_GROUP1);

    flags = fn_update_group_flags(flags, 5);   // F5 → group2
    assert(flags & FN_GROUP2);

    flags = fn_update_group_flags(flags, 10);  // F10 → group3
    assert(flags & FN_GROUP3);

    flags = fn_update_group_flags(flags, 15);  // F15 → group4
    assert(flags & FN_GROUP4);

    flags = fn_update_group_flags(flags, 25);  // F25 → group5
    assert(flags & FN_GROUP5);

    flags = fn_update_group_flags(flags, 32);  // F32 → group6
    assert(flags & FN_GROUP6);

    flags = fn_update_group_flags(flags, 40);  // F40 → group7
    assert(flags & FN_GROUP7);

    flags = fn_update_group_flags(flags, 48);  // F48 → group8
    assert(flags & FN_GROUP8);

    flags = fn_update_group_flags(flags, 56);  // F56 → group9
    assert(flags & FN_GROUP9);

    flags = fn_update_group_flags(flags, 64);  // F64 → group10
    assert(flags & FN_GROUP10);

    printf("  PASS: group flags\n");
}

static void test_encode_group1(void) {
    // F0=1, F2=1, F4=1 → functions bit0=1, bit2=1, bit4=1 → 0x15
    // Group1: 0x80 | ((f>>1) & 0x0F) | ((f & 0x01) << 4)
    // = 0x80 | (0x0A & 0x0F) | (1 << 4) = 0x80 | 0x0A | 0x10 = 0x9A
    uint64_t functions = 0x15;
    uint8_t byte = fn_encode_group1(functions);
    assert(byte == 0x9A);

    byte = fn_encode_group1(0);
    assert(byte == 0x80);

    printf("  PASS: encode group1\n");
}

static void test_encode_group2(void) {
    // F5=1, F6=1 → (1<<5) | (1<<6) = 0x60
    // Group2: 0xB0 | ((f>>5) & 0x0F) = 0xB0 | 0x03 = 0xB3
    uint64_t functions = ((uint64_t)1 << 5) | ((uint64_t)1 << 6);
    uint8_t byte = fn_encode_group2(functions);
    assert(byte == 0xB3);

    printf("  PASS: encode group2\n");
}

static void test_encode_group3(void) {
    // F9=1, F12=1 → (1<<9) | (1<<12) = 0x1200
    // Group3: 0xA0 | ((f>>9) & 0x0F) = 0xA0 | 0x09 = 0xA9
    uint64_t functions = ((uint64_t)1 << 9) | ((uint64_t)1 << 12);
    uint8_t byte = fn_encode_group3(functions);
    assert(byte == 0xA9);

    printf("  PASS: encode group3\n");
}

static void test_encode_group4(void) {
    // F13=1, F20=1 → bits 13 and 20 set; data byte = (f >> 13) & 0xFF
    // = (bit0 from F13) | (bit7 from F20) = 0x81
    uint64_t functions = ((uint64_t)1 << 13) | ((uint64_t)1 << 20);
    uint8_t byte = fn_encode_group4(functions);
    assert(byte == 0x81);

    printf("  PASS: encode group4\n");
}

static void test_encode_group5(void) {
    // F21=1, F28=1 → data byte = (f >> 21) & 0xFF
    // = (bit0 from F21) | (bit7 from F28) = 0x81
    uint64_t functions = ((uint64_t)1 << 21) | ((uint64_t)1 << 28);
    uint8_t byte = fn_encode_group5(functions);
    assert(byte == 0x81);

    printf("  PASS: encode group5\n");
}

static void test_encode_group6(void) {
    // F29=1, F36=1 → data byte = (f >> 29) & 0xFF = 0x81
    uint64_t functions = ((uint64_t)1 << 29) | ((uint64_t)1 << 36);
    uint8_t byte = fn_encode_group6(functions);
    assert(byte == 0x81);

    printf("  PASS: encode group6\n");
}

static void test_encode_group7(void) {
    // F37=1, F44=1 → data byte = (f >> 37) & 0xFF = 0x81
    uint64_t functions = ((uint64_t)1 << 37) | ((uint64_t)1 << 44);
    uint8_t byte = fn_encode_group7(functions);
    assert(byte == 0x81);

    printf("  PASS: encode group7\n");
}

static void test_encode_group8(void) {
    // F45=1, F52=1 → data byte = (f >> 45) & 0xFF = 0x81
    uint64_t functions = ((uint64_t)1 << 45) | ((uint64_t)1 << 52);
    uint8_t byte = fn_encode_group8(functions);
    assert(byte == 0x81);

    printf("  PASS: encode group8\n");
}

static void test_encode_group9(void) {
    // F53=1, F60=1 → data byte = (f >> 53) & 0xFF = 0x81
    uint64_t functions = ((uint64_t)1 << 53) | ((uint64_t)1 << 60);
    uint8_t byte = fn_encode_group9(functions);
    assert(byte == 0x81);

    printf("  PASS: encode group9\n");
}

static void test_encode_group10(void) {
    // F61 lives at bit 61 of functions_lo → goes to data bit 0
    // F68 lives at bit 4 of functions_hi  → goes to data bit 7
    // Expected: 0x81
    uint64_t lo = (uint64_t)1 << 61;
    uint8_t  hi = (uint8_t)(1u << 4);
    uint8_t byte = fn_encode_group10(lo, hi);
    assert(byte == 0x81);

    // F64 at functions_hi bit 0 → data bit 3 = 0x08
    byte = fn_encode_group10(0, (uint8_t)(1u << 0));
    assert(byte == 0x08);

    // F63 at functions_lo bit 63 → data bit 2 = 0x04
    byte = fn_encode_group10((uint64_t)1 << 63, 0);
    assert(byte == 0x04);

    printf("  PASS: encode group10\n");
}

int main(void) {
    printf("test_functions:\n");
    test_group_flags();
    test_encode_group1();
    test_encode_group2();
    test_encode_group3();
    test_encode_group4();
    test_encode_group5();
    test_encode_group6();
    test_encode_group7();
    test_encode_group8();
    test_encode_group9();
    test_encode_group10();
    printf("All function encoding tests passed.\n");
    return 0;
}
