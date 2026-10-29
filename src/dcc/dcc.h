#ifndef DCC_H
#define DCC_H

#include <stdbool.h>
#include <stdint.h>
#include "dcc/packet.h"
#include "board_config.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"

#define SHORT_ADDRESS_MAX 127
#define CMD_SET_SPEED     0x3F

typedef enum {
    SPEED_MODE_14  = 14,
    SPEED_MODE_28  = 28,
    SPEED_MODE_128 = 128,
} speed_mode_t;

typedef enum {
    LOOP_STATE_SPEED = 0,
    LOOP_STATE_FN_GROUP1,
    LOOP_STATE_FN_GROUP2,
    LOOP_STATE_FN_GROUP3,
    LOOP_STATE_FN_GROUP4,
    LOOP_STATE_FN_GROUP5,
    LOOP_STATE_FN_GROUP6,
    LOOP_STATE_FN_GROUP7,
    LOOP_STATE_FN_GROUP8,
    LOOP_STATE_FN_GROUP9,
    LOOP_STATE_FN_GROUP10,
    LOOP_STATE_RESTART,
} loop_state_t;

typedef struct {
    bool         active;
    uint16_t     address;
    bool         is_long_address; // short (1-127) vs 14-bit long; set at ensure_loco
    uint8_t      speed_step;     // bit 7 = direction, 6-0 = speed
    speed_mode_t speed_mode;
    uint64_t     functions;      // F0-F63 bitmask
    uint8_t      functions_hi;   // F64-F68 in bits 0-4
    uint16_t     group_flags;    // Which function groups have been touched (10 groups)
} loco_state_t;

typedef struct {
    loco_state_t     locos[MAX_LOCOS];
    loop_state_t     loop_state;
    SemaphoreHandle_t mutex;
    bool             estop_all;    // global e-stop held: every loco stopped, speeds kept
    QueueHandle_t    output_queue; // sends packets to priority queue

    // Most recent throttle command, for UI display. Updated under mutex.
    bool     last_throttle_valid;
    uint16_t last_throttle_addr;
    bool     last_throttle_is_long;
    uint8_t  last_throttle_speed_step; // bit 7 = direction, 6-0 = speed
} dcc_engine_t;

void dcc_init(dcc_engine_t *dcc, QueueHandle_t output_queue);

// Loco state management. These never expose loco_state_t pointers so that
// all access to per-loco fields stays inside dcc.c under the engine mutex.
void dcc_ensure_loco(dcc_engine_t *dcc, uint16_t address, bool is_long);
void dcc_forget_loco(dcc_engine_t *dcc, uint16_t address);

// Commands. `is_long` selects the DCC wire-address form (2-byte 11xxxxxx
// for long, 1-byte 0xxxxxxx for short). Addresses > 127 are always long
// regardless of the flag. For addresses 1-127, the flag is honored so
// decoders configured for long-address mode can be reached.
void dcc_set_throttle(dcc_engine_t *dcc, uint16_t address, bool is_long,
                      uint8_t speed, bool direction);
void dcc_set_function(dcc_engine_t *dcc, uint16_t address, bool is_long,
                      uint16_t fn_number, bool on);
// Emergency stop for one loco. Its stored speed becomes zero (direction
// kept), so the reminder loop doesn't start it again.
void dcc_emergency_stop(dcc_engine_t *dcc, uint16_t address, bool is_long);

// Global emergency stop: broadcasts e-stop, then holds every loco stopped
// until dcc_clear_emergency_stop_all. Stored speeds are kept, and throttle
// commands while held only change them; on clear the reminder loop sends
// each loco its stored speed again.
void dcc_emergency_stop_all(dcc_engine_t *dcc);
void dcc_clear_emergency_stop_all(dcc_engine_t *dcc);
bool dcc_emergency_stop_all_held(dcc_engine_t *dcc);

// Programming-on-Main byte write. Emits a single NMRA POM write-byte
// packet (repeated `repeats` times on the wire — 3 is the NMRA-
// recommended minimum) and returns. Unlike throttle/function commands
// this does NOT touch loco state and does NOT enter the reminder loop;
// the decoder commits the CV change on the first successfully received
// copy and further repeats are harmless. `cv` is 1-based.
void dcc_pom_write_byte(dcc_engine_t *dcc, uint16_t address, bool is_long,
                        uint16_t cv, uint8_t value);

// CV writes on the main that S-9.2.1 gives their own instruction, sent
// like dcc_pom_write_byte (3 repeats, no loco state touched):
//   - CV17 and CV18 together: the Long Address instruction, which also
//     sets CV29 bit 5 so the decoder answers to its new long address.
//   - CV19: Consist Control. Bit 7 means reversed in the consist; a
//     consist address (bits 0-6) of 0 takes the loco out of its consist.
//   - CV19 and CV20 together: the Consist Address short form.
void dcc_pom_write_long_address(dcc_engine_t *dcc, uint16_t address,
                                bool is_long, uint8_t cv17, uint8_t cv18);
void dcc_pom_write_consist(dcc_engine_t *dcc, uint16_t address, bool is_long,
                           uint8_t cv19);
void dcc_pom_write_long_consist(dcc_engine_t *dcc, uint16_t address,
                                bool is_long, uint8_t cv19, uint8_t cv20);

// Programming-on-Main read: a POM Verify Byte, sent `repeats` + 1 times
// back to back. A RailCom decoder answers in the following cutouts.
void dcc_pom_read_byte(dcc_engine_t *dcc, uint16_t address, bool is_long,
                       uint16_t cv, int8_t repeats);

// Reminder loop (called periodically from task_dcc_reminder)
void dcc_update(dcc_engine_t *dcc);

// Reports the most recent throttle command, if any. Returns false if no
// throttle has been issued since boot.
bool dcc_get_last_throttle(dcc_engine_t *dcc, uint16_t *address,
                           bool *is_long, uint8_t *speed_step);

// FreeRTOS task
void task_dcc_reminder(void *params);

#endif // DCC_H
