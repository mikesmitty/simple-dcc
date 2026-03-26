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
    LOOP_STATE_RESTART,
} loop_state_t;

typedef struct {
    bool         active;
    uint16_t     address;
    uint8_t      speed_step;   // bit 7 = direction, 6-0 = speed
    speed_mode_t speed_mode;
    uint32_t     functions;    // F0-F31 bitmask
    uint8_t      group_flags;  // Which function groups have been touched
} loco_state_t;

typedef struct {
    loco_state_t     locos[MAX_LOCOS];
    loop_state_t     loop_state;
    SemaphoreHandle_t mutex;
    QueueHandle_t    output_queue; // sends packets to priority queue
} dcc_engine_t;

void dcc_init(dcc_engine_t *dcc, QueueHandle_t output_queue);

// Loco state management
loco_state_t *dcc_get_or_create_loco(dcc_engine_t *dcc, uint16_t address);
loco_state_t *dcc_get_loco(dcc_engine_t *dcc, uint16_t address);
void          dcc_forget_loco(dcc_engine_t *dcc, uint16_t address);

// Commands
void dcc_set_throttle(dcc_engine_t *dcc, uint16_t address,
                      uint8_t speed, bool direction);
void dcc_set_function(dcc_engine_t *dcc, uint16_t address,
                      uint16_t fn_number, bool on);
void dcc_emergency_stop(dcc_engine_t *dcc, uint16_t address);
void dcc_emergency_stop_all(dcc_engine_t *dcc);

// Reminder loop (called periodically from task_dcc_reminder)
void dcc_update(dcc_engine_t *dcc);

// FreeRTOS task
void task_dcc_reminder(void *params);

#endif // DCC_H
