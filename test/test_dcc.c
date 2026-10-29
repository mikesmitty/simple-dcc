#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "dcc/packet.h"

/* ---- output queue capture ---------------------------------------------- */
/* The engine hands packets to the priority queue with xQueueSend. Record
 * each one's bytes and priority, then return it to the pool. */

typedef struct {
    uint8_t        data[MAX_PACKET_DATA];
    uint8_t        len;
    dcc_priority_t priority;
} sent_pkt_t;

static sent_pkt_t sent[64];
static int        sent_count;

static int xQueueSend(QueueHandle_t q, const void *item, TickType_t t) {
    (void)q; (void)t;
    dcc_packet_t *pkt = *(dcc_packet_t *const *)item;
    if (sent_count < 64) {
        memcpy(sent[sent_count].data, pkt->data, pkt->len);
        sent[sent_count].len = pkt->len;
        sent[sent_count].priority = pkt->priority;
        sent_count++;
    }
    packet_free(pkt);
    return pdTRUE;
}

#include "../src/dcc/dcc.c"

/* ---- helpers ------------------------------------------------------------ */

static dcc_engine_t dcc;

static void reset_all(void) {
    packet_pool_init();
    dcc_init(&dcc, (QueueHandle_t)1);
    sent_count = 0;
}

static bool sent_is(int i, const uint8_t *want, uint8_t len) {
    return i < sent_count && sent[i].len == len &&
           memcmp(sent[i].data, want, len) == 0;
}

/* One pass of the reminder loop's speed state. */
static void speed_reminders(void) {
    sent_count = 0;
    dcc.loop_state = LOOP_STATE_SPEED;
    dcc_update(&dcc);
}

/* ---- tests -------------------------------------------------------------- */

/* A loco's e-stop zeroes its stored speed, keeping direction, so the next
 * speed reminder doesn't restart it. */
static void test_estop_stays_stopped(void) {
    reset_all();

    dcc_set_throttle(&dcc, 3, false, 50, true);
    const uint8_t run[] = { 0x03, 0x3F, 0x80 | 50 };
    assert(sent_is(0, run, sizeof(run)));

    sent_count = 0;
    dcc_emergency_stop(&dcc, 3, false);
    const uint8_t estop[] = { 0x03, 0x3F, 0x81 };
    assert(sent_count == 1 && sent_is(0, estop, sizeof(estop)));
    assert(sent[0].priority == PRIORITY_EMERGENCY);

    speed_reminders();
    const uint8_t stopped[] = { 0x03, 0x3F, 0x80 };
    assert(sent_count == 1 && sent_is(0, stopped, sizeof(stopped)));

    uint16_t addr; bool is_long; uint8_t step;
    assert(dcc_get_last_throttle(&dcc, &addr, &is_long, &step));
    assert(addr == 3 && step == 0x80);

    /* Reverse keeps its direction too. */
    dcc_set_throttle(&dcc, 3, false, 20, false);
    sent_count = 0;
    dcc_emergency_stop(&dcc, 3, false);
    const uint8_t estop_rev[] = { 0x03, 0x3F, 0x01 };
    assert(sent_is(0, estop_rev, sizeof(estop_rev)));
    speed_reminders();
    const uint8_t stopped_rev[] = { 0x03, 0x3F, 0x00 };
    assert(sent_is(0, stopped_rev, sizeof(stopped_rev)));

    printf("  PASS: e-stop stays stopped\n");
}

/* Global e-stop broadcasts, then holds every loco stopped without losing
 * its speed. Throttle commands while held only change the stored speed;
 * clearing sends each loco its speed again. */
static void test_global_estop_hold(void) {
    reset_all();

    dcc_set_throttle(&dcc, 3, false, 50, true);
    dcc_set_throttle(&dcc, 150, true, 20, false);
    assert(!dcc_emergency_stop_all_held(&dcc));

    sent_count = 0;
    dcc_emergency_stop_all(&dcc);
    const uint8_t bcast[] = { 0x00, 0x71 };
    assert(sent_count == 1 && sent_is(0, bcast, sizeof(bcast)));
    assert(dcc_emergency_stop_all_held(&dcc));

    const uint8_t estop3[]   = { 0x03, 0x3F, 0x81 };
    const uint8_t estop150[] = { 0xC0, 0x96, 0x3F, 0x01 };
    speed_reminders();
    assert(sent_count == 2);
    assert(sent_is(0, estop3, sizeof(estop3)));
    assert(sent_is(1, estop150, sizeof(estop150)));
    assert(sent[0].priority == PRIORITY_EMERGENCY);

    /* A new speed while held goes out only as e-stop. */
    sent_count = 0;
    dcc_set_throttle(&dcc, 3, false, 60, true);
    assert(sent_count == 0);
    speed_reminders();
    assert(sent_is(0, estop3, sizeof(estop3)));

    /* Function reminders carry on as usual. */
    dcc_set_function(&dcc, 3, false, 0, true);
    sent_count = 0;
    dcc.loop_state = LOOP_STATE_FN_GROUP1;
    dcc_update(&dcc);
    const uint8_t f0[] = { 0x03, 0x90 };
    assert(sent_count == 1 && sent_is(0, f0, sizeof(f0)));

    dcc_clear_emergency_stop_all(&dcc);
    assert(!dcc_emergency_stop_all_held(&dcc));
    speed_reminders();
    const uint8_t run3[]   = { 0x03, 0x3F, 0x80 | 60 };
    const uint8_t run150[] = { 0xC0, 0x96, 0x3F, 20 };
    assert(sent_count == 2);
    assert(sent_is(0, run3, sizeof(run3)));
    assert(sent_is(1, run150, sizeof(run150)));
    assert(sent[0].priority == PRIORITY_LOW);

    printf("  PASS: global e-stop hold\n");
}

int main(void) {
    printf("test_dcc:\n");
    test_estop_stays_stopped();
    test_global_estop_hold();
    printf("All DCC engine tests passed.\n");
    return 0;
}
