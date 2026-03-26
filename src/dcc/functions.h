#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <stdint.h>

// Function group bitmask flags (stored in loco_state_t.group_flags)
#define FN_GROUP1  (1 << 0) // F0-F4
#define FN_GROUP2  (1 << 1) // F5-F8
#define FN_GROUP3  (1 << 2) // F9-F12
#define FN_GROUP4  (1 << 3) // F13-F20
#define FN_GROUP5  (1 << 4) // F21-F28

uint8_t fn_update_group_flags(uint8_t flags, uint16_t fn_number);
uint8_t fn_encode_group1(uint32_t functions);
uint8_t fn_encode_group2(uint32_t functions);
uint8_t fn_encode_group3(uint32_t functions);

#endif // FUNCTIONS_H
