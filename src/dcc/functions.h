#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <stdint.h>

// Function group bitmask flags (stored in loco_state_t.group_flags).
// Groups 6-10 use the RCN-212 extended two-byte form (0xD8-0xDC + data byte).
#define FN_GROUP1   (1u << 0)  // F0-F4
#define FN_GROUP2   (1u << 1)  // F5-F8
#define FN_GROUP3   (1u << 2)  // F9-F12
#define FN_GROUP4   (1u << 3)  // F13-F20
#define FN_GROUP5   (1u << 4)  // F21-F28
#define FN_GROUP6   (1u << 5)  // F29-F36
#define FN_GROUP7   (1u << 6)  // F37-F44
#define FN_GROUP8   (1u << 7)  // F45-F52
#define FN_GROUP9   (1u << 8)  // F53-F60
#define FN_GROUP10  (1u << 9)  // F61-F68

#define MAX_FN_NUMBER 68

// Updates group flags for the affected function number (0..68).
uint16_t fn_update_group_flags(uint16_t flags, uint16_t fn_number);

// Groups 1-3 encode function state directly into the command byte (short form).
// The returned byte is the complete DCC instruction byte.
uint8_t fn_encode_group1(uint64_t functions);  // F0-F4,  returns 0x80-0x9F
uint8_t fn_encode_group2(uint64_t functions);  // F5-F8,  returns 0xB0-0xBF
uint8_t fn_encode_group3(uint64_t functions);  // F9-F12, returns 0xA0-0xAF

// Groups 4-10 use a two-byte form: a fixed command byte followed by one data byte.
// These helpers return the data byte; the command byte is a per-group constant.
uint8_t fn_encode_group4(uint64_t functions);   // F13-F20, command 0xDE
uint8_t fn_encode_group5(uint64_t functions);   // F21-F28, command 0xDF
uint8_t fn_encode_group6(uint64_t functions);   // F29-F36, command 0xD8
uint8_t fn_encode_group7(uint64_t functions);   // F37-F44, command 0xD9
uint8_t fn_encode_group8(uint64_t functions);   // F45-F52, command 0xDA
uint8_t fn_encode_group9(uint64_t functions);   // F53-F60, command 0xDB
uint8_t fn_encode_group10(uint64_t functions_lo, uint8_t functions_hi); // F61-F68, command 0xDC

#endif // FUNCTIONS_H
