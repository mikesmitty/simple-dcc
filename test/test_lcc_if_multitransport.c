/* Host test for the transport abstraction in src/lcc/lcc_if.c.
 *
 * Verifies that:
 *   - lcc_if_init() auto-registers the built-in USB egress
 *   - lcc_if_register_transport() adds additional egresses up to
 *     LCC_IF_MAX_EGRESS and refuses further ones
 *   - lcc_if_tx() fans a frame out to every registered egress
 *   - lcc_if_enqueue_frame() + lcc_if_rx() round-trips
 *   - lcc_if_on_rx_byte() feeds the GridConnect parser and enqueues on
 *     completed frames
 *
 * The test links lcc_if.c directly against a local FreeRTOS + serial
 * mock; no Pico SDK or real RTOS involved.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- FreeRTOS host mock ------------------------------------------------- */
/* The shared test/FreeRTOS.h declares the opaque handle types + mutex
 * stubs but not the queue API that lcc_if.c uses. Provide a small
 * functional ring-buffer queue here so xQueueSend / xQueueReceive
 * actually transport data. */

#include "FreeRTOS.h"

typedef struct {
    uint8_t buf[16 * 64];   /* 16 slots × max(sizeof(lcc_frame_t)) — 64
                               is generous; real frame is smaller. */
    size_t  item_size;
    size_t  capacity;
    size_t  head;
    size_t  tail;
    size_t  count;
} mock_queue_t;

static mock_queue_t g_mock_queue;

static inline QueueHandle_t xQueueCreate(size_t capacity, size_t item_size) {
    memset(&g_mock_queue, 0, sizeof(g_mock_queue));
    assert(capacity * item_size <= sizeof(g_mock_queue.buf));
    g_mock_queue.capacity  = capacity;
    g_mock_queue.item_size = item_size;
    return (QueueHandle_t)&g_mock_queue;
}
static inline int xQueueSend(QueueHandle_t q, const void *item, TickType_t t) {
    (void)t;
    mock_queue_t *mq = (mock_queue_t *)q;
    if (mq->count >= mq->capacity) return pdFALSE;
    memcpy(&mq->buf[mq->tail * mq->item_size], item, mq->item_size);
    mq->tail = (mq->tail + 1) % mq->capacity;
    mq->count++;
    return pdTRUE;
}
static inline int xQueueReceive(QueueHandle_t q, void *out, TickType_t t) {
    (void)t;
    mock_queue_t *mq = (mock_queue_t *)q;
    if (mq->count == 0) return pdFALSE;
    memcpy(out, &mq->buf[mq->head * mq->item_size], mq->item_size);
    mq->head = (mq->head + 1) % mq->capacity;
    mq->count--;
    return pdTRUE;
}

/* --- serial mock -------------------------------------------------------- */

static bool     g_serial_ready   = true;
static uint8_t  g_serial_log[256];
static uint16_t g_serial_log_len;

bool serial_write_ready(void) { return g_serial_ready; }
void serial_write(const uint8_t *buf, uint16_t n) {
    assert(g_serial_log_len + n <= sizeof(g_serial_log));
    memcpy(&g_serial_log[g_serial_log_len], buf, n);
    g_serial_log_len += n;
}

/* --- lcc_node mocks (only lcc_if_dispatch uses these; we don't test
 *     dispatch here, so leave them as no-ops) ------------------------- */

#include "lcc/lcc_node.h"
#include "lcc/lcc_train_search.h"

int          lcc_node_count(void) { return 0; }
lcc_node_t  *lcc_node_at(int i) { (void)i; return NULL; }
lcc_node_t  *lcc_node_find_by_alias(uint16_t a) { (void)a; return NULL; }
void         lcc_node_handle_frame(lcc_node_t *n, const lcc_frame_t *f) {
    (void)n; (void)f;
}
void lcc_train_search_dispatch(uint16_t src, uint64_t ev) { (void)src; (void)ev; }
void lcc_train_search_ensure_by_node_id(uint64_t id)      { (void)id; }

/* --- lcc_if.c under test ----------------------------------------------- */

#include "../src/lcc/lcc_if.c"

/* --- recording egress --------------------------------------------------- */

static int          g_rec_count[4];
static lcc_frame_t  g_rec_last[4];
static bool         g_rec_ret[4] = { true, true, true, true };

static bool rec_egress_0(const lcc_frame_t *f, void *ctx) {
    (void)ctx; g_rec_count[0]++; g_rec_last[0] = *f; return g_rec_ret[0];
}
static bool rec_egress_1(const lcc_frame_t *f, void *ctx) {
    (void)ctx; g_rec_count[1]++; g_rec_last[1] = *f; return g_rec_ret[1];
}
static bool rec_egress_2(const lcc_frame_t *f, void *ctx) {
    (void)ctx; g_rec_count[2]++; g_rec_last[2] = *f; return g_rec_ret[2];
}
static bool rec_egress_3(const lcc_frame_t *f, void *ctx) {
    (void)ctx; g_rec_count[3]++; g_rec_last[3] = *f; return g_rec_ret[3];
}

/* --- helpers ------------------------------------------------------------ */

static void reset_state(void) {
    memset(g_rec_count, 0, sizeof(g_rec_count));
    memset(g_rec_last,  0, sizeof(g_rec_last));
    g_rec_ret[0] = g_rec_ret[1] = g_rec_ret[2] = g_rec_ret[3] = true;
    memset(g_serial_log, 0, sizeof(g_serial_log));
    g_serial_log_len = 0;
    g_serial_ready = true;
    egress_count = 0;   /* re-run init fresh */
    memset(&gc_parser, 0, sizeof(gc_parser));
}

/* --- tests -------------------------------------------------------------- */

static void test_init_registers_usb_egress(void) {
    reset_state();
    lcc_if_init();
    assert(egress_count == 1);
    printf("  ok: init registers USB egress (count=%u)\n", egress_count);
}

static void test_register_until_full(void) {
    reset_state();
    lcc_if_init();   /* seeds 1 entry */

    bool a = lcc_if_register_transport(rec_egress_1, NULL);
    bool b = lcc_if_register_transport(rec_egress_2, NULL);
    bool c = lcc_if_register_transport(rec_egress_3, NULL);  /* should fail at 3 */

    assert(a == true);
    assert(b == true);
    assert(c == false);  /* LCC_IF_MAX_EGRESS = 3 */
    assert(egress_count == LCC_IF_MAX_EGRESS);
    printf("  ok: register_transport respects LCC_IF_MAX_EGRESS=%d\n",
           LCC_IF_MAX_EGRESS);
}

static void test_register_null_rejected(void) {
    reset_state();
    lcc_if_init();
    bool ok = lcc_if_register_transport(NULL, NULL);
    assert(ok == false);
    printf("  ok: register_transport rejects NULL fn\n");
}

static void test_tx_fanout_reaches_all_registered(void) {
    reset_state();
    lcc_if_init();          /* usb_egress at slot 0 */
    g_serial_ready = false; /* drop from USB silently so we only see
                               recording egresses in serial log */

    lcc_if_register_transport(rec_egress_1, NULL);
    lcc_if_register_transport(rec_egress_2, NULL);

    lcc_frame_t f = { .id = 0x10123456, .dlc = 3,
                      .data = { 0xDE, 0xAD, 0xBE } };
    bool any_ok = lcc_if_tx(&f);
    assert(any_ok == true);

    /* slot 1 + slot 2 are our recording egresses */
    assert(g_rec_count[1] == 1);
    assert(g_rec_count[2] == 1);
    assert(g_rec_last[1].id == 0x10123456);
    assert(g_rec_last[1].dlc == 3);
    assert(g_rec_last[1].data[0] == 0xDE);
    assert(g_rec_last[2].id == 0x10123456);
    printf("  ok: tx fans out to every registered egress\n");
}

static void test_tx_any_ok_reflects_egress_returns(void) {
    reset_state();
    lcc_if_init();
    g_serial_ready = true;  /* usb egress returns true */

    lcc_if_register_transport(rec_egress_1, NULL);
    g_rec_ret[1] = false;

    lcc_frame_t f = { .id = 1, .dlc = 0 };
    bool any_ok = lcc_if_tx(&f);
    assert(any_ok == true);  /* usb egress succeeded */
    assert(g_rec_count[1] == 1);

    /* Now flip USB off too; all egresses fail → any_ok = false */
    g_serial_ready = false;  /* but usb_egress treats this as tolerated
                                 silent drop and returns true */
    g_rec_ret[1] = false;
    any_ok = lcc_if_tx(&f);
    assert(any_ok == true);  /* usb still returns true on disconnected */
    printf("  ok: tx any_ok reflects egress returns\n");
}

static void test_enqueue_frame_round_trips(void) {
    reset_state();
    lcc_if_init();

    lcc_frame_t in = { .id = 0x19490C77, .dlc = 2,
                       .data = { 0x01, 0x02 } };
    lcc_if_enqueue_frame(&in);

    lcc_frame_t out;
    memset(&out, 0, sizeof(out));
    bool got = lcc_if_rx(&out, 0);
    assert(got == true);
    assert(out.id == in.id);
    assert(out.dlc == in.dlc);
    assert(out.data[0] == 0x01 && out.data[1] == 0x02);
    printf("  ok: enqueue_frame + rx round-trips\n");
}

static void test_on_rx_byte_parses_gridconnect(void) {
    reset_state();
    lcc_if_init();

    /* Minimal GridConnect line: `:X<id-hex>N<data-hex>;`. Payload is
     * two data bytes so dlc should decode to 2. */
    const char *line = ":X10123456N0102;";
    for (const char *p = line; *p; p++)
        lcc_if_on_rx_byte((uint8_t)*p);

    lcc_frame_t out;
    bool got = lcc_if_rx(&out, 0);
    assert(got == true);
    assert(out.id == 0x10123456);
    assert(out.dlc == 2);
    assert(out.data[0] == 0x01 && out.data[1] == 0x02);
    printf("  ok: on_rx_byte → gc parser → enqueue_frame\n");
}

int main(void) {
    printf("test_lcc_if_multitransport\n");
    test_init_registers_usb_egress();
    test_register_until_full();
    test_register_null_rejected();
    test_tx_fanout_reaches_all_registered();
    test_tx_any_ok_reflects_egress_returns();
    test_enqueue_frame_round_trips();
    test_on_rx_byte_parses_gridconnect();
    printf("all ok\n");
    return 0;
}
