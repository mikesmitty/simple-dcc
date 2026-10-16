#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "lcc/lcc_memconfig.h"
#include "lcc/lcc_datagram.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_node.h"

/* ---- lcc_datagram mocks ------------------------------------------------- */

typedef struct {
    uint16_t dst_alias;
    uint8_t  data[72];
    uint8_t  len;
} dg_rec_t;

static dg_rec_t g_sent[8];
static int      g_sent_count;

void lcc_datagram_send(const lcc_node_t *node, uint16_t dst_alias,
                       const uint8_t *data, uint8_t len) {
    (void)node;
    assert(g_sent_count < (int)(sizeof(g_sent) / sizeof(g_sent[0])));
    dg_rec_t *r = &g_sent[g_sent_count++];
    r->dst_alias = dst_alias;
    r->len = len;
    memcpy(r->data, data, len);
}
void lcc_datagram_send_ok(const lcc_node_t *node, uint16_t dst_alias) {
    (void)node; (void)dst_alias;
}

/* ---- spaces under test -------------------------------------------------- */

static int                      g_defer_calls;
static lcc_memconfig_deferred_t g_last_req;
static uint8_t                  g_last_req_data[8];
static uint16_t                 g_defer_return;
static bool                     g_complete_inline;  /* finish inside defer */

static uint16_t mock_defer(lcc_node_t *node,
                           const lcc_memconfig_deferred_t *req) {
    (void)node;
    g_defer_calls++;
    g_last_req = *req;
    if (req->data)
        memcpy(g_last_req_data, req->data, req->count);
    if (g_complete_inline && g_defer_return == 0) {
        uint8_t v = 0x5A;
        lcc_memconfig_complete(0, &v, 1);
    }
    return g_defer_return;
}

static const lcc_memspace_handler_t g_node_spaces[] = {
    { .space = 0xF8, .size = 1024, .defer = mock_defer },
};

static uint8_t g_cfg[16] = { 0x10, 0x11, 0x12, 0x13 };
static uint16_t cfg_read(lcc_node_t *node, uint32_t addr, uint16_t count,
                         uint8_t *out) {
    (void)node;
    memcpy(out, &g_cfg[addr], count);
    return count;
}
static const lcc_memspace_handler_t g_global_spaces[] = {
    { .space = 0xFD, .size = sizeof(g_cfg), .read = cfg_read },
};

static lcc_node_t g_node;

static void reset_all(void) {
    memset(g_sent, 0, sizeof(g_sent));
    g_sent_count = 0;
    g_defer_calls = 0;
    memset(&g_last_req, 0, sizeof(g_last_req));
    memset(g_last_req_data, 0, sizeof(g_last_req_data));
    g_defer_return = 0;
    g_complete_inline = false;

    memset(&g_node, 0, sizeof(g_node));
    g_node.alias = 0xABC;
    g_node.alias_state = LCC_A_CONFIRMED;
    lcc_memconfig_register_node_spaces(&g_node, g_node_spaces, 1);
    lcc_memconfig_register_spaces(g_global_spaces, 1);
}

/* [0x20][0x40][addr:4][space][count] */
static uint8_t mc_read(uint8_t *buf, uint32_t addr, uint8_t space,
                       uint8_t count) {
    buf[0] = 0x20; buf[1] = 0x40;
    buf[2] = (uint8_t)(addr >> 24); buf[3] = (uint8_t)(addr >> 16);
    buf[4] = (uint8_t)(addr >> 8);  buf[5] = (uint8_t)addr;
    buf[6] = space; buf[7] = count;
    return 8;
}

/* [0x20][0x00][addr:4][space][data...] */
static uint8_t mc_write(uint8_t *buf, uint32_t addr, uint8_t space,
                        const uint8_t *data, uint8_t n) {
    buf[0] = 0x20; buf[1] = 0x00;
    buf[2] = (uint8_t)(addr >> 24); buf[3] = (uint8_t)(addr >> 16);
    buf[4] = (uint8_t)(addr >> 8);  buf[5] = (uint8_t)addr;
    buf[6] = space;
    if (n) memcpy(&buf[7], data, n);
    return (uint8_t)(7 + n);
}

static void assert_bytes(const dg_rec_t *r, const uint8_t *want, uint8_t n) {
    assert(r->dst_alias == 0x123);
    assert(r->len == n);
    assert(memcmp(r->data, want, n) == 0);
}

/* ---- tests -------------------------------------------------------------- */

static void test_ack_flags(void) {
    reset_all();
    uint8_t pkt[16];
    uint8_t one = 1;

    uint8_t len = mc_read(pkt, 0, 0xF8, 1);
    assert(lcc_memconfig_ack_flags(&g_node, pkt, len) ==
           (LCC_DATAGRAM_OK_REPLY_PENDING | LCC_MEMCFG_DEFER_TIMEOUT_EXP));
    len = mc_write(pkt, 0, 0xF8, &one, 1);
    assert(lcc_memconfig_ack_flags(&g_node, pkt, len) ==
           (LCC_DATAGRAM_OK_REPLY_PENDING | LCC_MEMCFG_DEFER_TIMEOUT_EXP));

    len = mc_read(pkt, 0, 0xFD, 1);              /* synchronous space */
    assert(lcc_memconfig_ack_flags(&g_node, pkt, len) == 0);

    uint8_t info[3] = { 0x20, 0x84, 0xF8 };      /* not a read/write */
    assert(lcc_memconfig_ack_flags(&g_node, info, sizeof(info)) == 0);

    /* Malformed requests handle_read/handle_write drop: no promise. */
    len = mc_read(pkt, 0, 0xF8, 0);              /* zero count */
    assert(lcc_memconfig_ack_flags(&g_node, pkt, len) == 0);
    assert(lcc_memconfig_ack_flags(&g_node, pkt, 7) == 0);  /* no count */
    len = mc_write(pkt, 0, 0xF8, NULL, 0);       /* no data */
    assert(lcc_memconfig_ack_flags(&g_node, pkt, len) == 0);

    printf("  PASS: Reply Pending only for deferred reads/writes\n");
}

static void test_deferred_read_success(void) {
    reset_all();
    uint8_t pkt[16];
    uint8_t len = mc_read(pkt, 28, 0xF8, 1);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);

    assert(g_defer_calls == 1);
    assert(!g_last_req.is_write);
    assert(g_last_req.space == 0xF8);
    assert(g_last_req.addr == 28);
    assert(g_last_req.count == 1);
    assert(g_last_req.data == NULL);
    assert(g_sent_count == 0);                   /* nothing until done */
    assert(lcc_memconfig_deferred_pending());

    lcc_memconfig_flush_deferred();
    assert(g_sent_count == 0);

    uint8_t v = 0x06;
    lcc_memconfig_complete(0, &v, 1);
    assert(g_sent_count == 0);                   /* reply waits for flush */
    assert(lcc_memconfig_deferred_pending());    /* still owed a reply */

    lcc_memconfig_flush_deferred();
    assert(!lcc_memconfig_deferred_pending());
    const uint8_t want[] = { 0x20, 0x50, 0, 0, 0, 28, 0xF8, 0x06 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));

    lcc_memconfig_flush_deferred();              /* one reply only */
    assert(g_sent_count == 1);
    printf("  PASS: deferred read replies with data after flush\n");
}

static void test_deferred_read_failure(void) {
    reset_all();
    uint8_t pkt[16];
    uint8_t len = mc_read(pkt, 0, 0xF8, 1);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);

    lcc_memconfig_complete(LCC_ERR_DCC_NO_LOCO, NULL, 0);
    lcc_memconfig_flush_deferred();

    const uint8_t want[] = { 0x20, 0x58, 0, 0, 0, 0, 0xF8, 0x20, 0x31 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));
    printf("  PASS: deferred read failure carries the error code\n");
}

static void test_deferred_write_success(void) {
    reset_all();
    uint8_t pkt[16];
    uint8_t val = 0x03;
    uint8_t len = mc_write(pkt, 0, 0xF8, &val, 1);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);

    assert(g_defer_calls == 1);
    assert(g_last_req.is_write);
    assert(g_last_req.addr == 0);
    assert(g_last_req.count == 1);
    assert(g_last_req_data[0] == 0x03);

    lcc_memconfig_complete(0, NULL, 0);
    lcc_memconfig_flush_deferred();

    const uint8_t want[] = { 0x20, 0x10, 0, 0, 0, 0, 0xF8 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));
    printf("  PASS: deferred write sends Write Reply OK\n");
}

static void test_deferred_write_failure(void) {
    reset_all();
    uint8_t pkt[16];
    uint8_t val = 0x03;
    uint8_t len = mc_write(pkt, 0x0102, 0xF8, &val, 1);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);

    lcc_memconfig_complete(LCC_ERR_DCC_FAILED_VERIFY, NULL, 0);
    lcc_memconfig_flush_deferred();

    const uint8_t want[] = { 0x20, 0x18, 0, 0, 0x01, 0x02, 0xF8, 0x20, 0x32 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));
    printf("  PASS: deferred write failure carries the error code\n");
}

static void test_second_request_while_pending_is_busy(void) {
    reset_all();
    uint8_t pkt[16];
    uint8_t len = mc_read(pkt, 0, 0xF8, 1);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    assert(g_defer_calls == 1);

    len = mc_read(pkt, 7, 0xF8, 1);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    assert(g_defer_calls == 1);                  /* not handed over */
    const uint8_t busy[] = { 0x20, 0x58, 0, 0, 0, 7, 0xF8, 0x20, 0x00 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], busy, sizeof(busy));

    uint8_t v = 0x11;                            /* first one still lands */
    lcc_memconfig_complete(0, &v, 1);
    lcc_memconfig_flush_deferred();
    const uint8_t want[] = { 0x20, 0x50, 0, 0, 0, 0, 0xF8, 0x11 };
    assert(g_sent_count == 2);
    assert_bytes(&g_sent[1], want, sizeof(want));
    printf("  PASS: second deferred request gets a temporary error\n");
}

static void test_defer_error_replies_now(void) {
    reset_all();
    g_defer_return = LCC_ERR_DCC_PGM_DISABLED;
    uint8_t pkt[16];
    uint8_t len = mc_read(pkt, 0, 0xF8, 1);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);

    const uint8_t want[] = { 0x20, 0x58, 0, 0, 0, 0, 0xF8, 0x10, 0x21 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));

    /* Slot was released: the next request is handed over. */
    g_defer_return = 0;
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    assert(g_defer_calls == 2);
    lcc_memconfig_complete(LCC_ERR_TEMPORARY, NULL, 0);
    lcc_memconfig_flush_deferred();
    printf("  PASS: defer error is replied immediately and frees the slot\n");
}

static void test_out_of_bounds(void) {
    reset_all();
    uint8_t pkt[16];
    uint8_t len = mc_read(pkt, 1024, 0xF8, 1);   /* CV 1025 */
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);

    assert(g_defer_calls == 0);
    const uint8_t want[] = { 0x20, 0x58, 0, 0, 0x04, 0x00, 0xF8, 0x10, 0x82 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));
    printf("  PASS: out-of-range deferred address is rejected\n");
}

static void test_complete_inside_defer(void) {
    reset_all();
    g_complete_inline = true;
    uint8_t pkt[16];
    uint8_t len = mc_read(pkt, 3, 0xF8, 1);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    assert(g_sent_count == 0);

    lcc_memconfig_flush_deferred();
    const uint8_t want[] = { 0x20, 0x50, 0, 0, 0, 3, 0xF8, 0x5A };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));
    printf("  PASS: completion inside the defer hook still replies\n");
}

static void test_stray_complete_ignored(void) {
    reset_all();
    uint8_t v = 0x99;
    lcc_memconfig_complete(0, &v, 1);
    lcc_memconfig_flush_deferred();
    assert(g_sent_count == 0);
    printf("  PASS: completion with nothing pending is ignored\n");
}

static void test_sync_space_unchanged(void) {
    reset_all();
    uint8_t pkt[8] = { 0x20, 0x41, 0, 0, 0, 1, 2 };  /* read 0xFD @1, 2 bytes */
    lcc_memconfig_handle(&g_node, 0x123, pkt, 7);

    const uint8_t want[] = { 0x20, 0x51, 0, 0, 0, 1, 0x11, 0x12 };
    assert(g_defer_calls == 0);
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));
    printf("  PASS: synchronous spaces still reply inline\n");
}

int main(void) {
    printf("test_lcc_memconfig:\n");
    test_ack_flags();
    test_deferred_read_success();
    test_deferred_read_failure();
    test_deferred_write_success();
    test_deferred_write_failure();
    test_second_request_while_pending_is_busy();
    test_defer_error_replies_now();
    test_out_of_bounds();
    test_complete_inside_defer();
    test_stray_complete_ignored();
    test_sync_space_unchanged();
    printf("All LCC memconfig tests passed.\n");
    return 0;
}
