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

static const uint8_t g_cfg_init[16] = { 0x10, 0x11, 0x12, 0x13 };
static uint8_t g_cfg[16];
static uint16_t g_cfg_read_error;  /* nonzero: cfg_read fails with it */
static uint16_t cfg_read(lcc_node_t *node, uint32_t addr, uint16_t count,
                         uint8_t *out, uint16_t *error) {
    (void)node;
    if (g_cfg_read_error) {
        *error = g_cfg_read_error;
        return 0;
    }
    memcpy(out, &g_cfg[addr], count);
    return count;
}
static int      g_cfg_writes;
static uint16_t g_cfg_write_max;    /* nonzero: take at most this many */
static uint32_t g_cfg_write_fail_at = UINT32_MAX;  /* fail at this addr */
static uint16_t g_cfg_write_error;  /* ...with this (0: no progress) */
static uint16_t cfg_write(lcc_node_t *node, uint32_t addr, uint16_t count,
                          const uint8_t *in, uint16_t *error) {
    (void)node;
    g_cfg_writes++;
    if (addr == g_cfg_write_fail_at) {
        *error = g_cfg_write_error;
        return 0;
    }
    if (g_cfg_write_max && count > g_cfg_write_max)
        count = g_cfg_write_max;
    memcpy(&g_cfg[addr], in, count);
    return count;
}
static const lcc_memspace_handler_t g_global_spaces[] = {
    { .space = 0xFD, .size = sizeof(g_cfg), .read = cfg_read,
      .write = cfg_write },
    { .space = 0xFC, .size = 4, .read_only = true, .read = cfg_read },
};

static int g_reboots;
static void mock_reboot(void) { g_reboots++; }
static int g_factory_resets;
static void mock_factory_reset(lcc_node_t *node) {
    (void)node;
    g_factory_resets++;
}

static lcc_node_t g_node;

static void reset_all(void) {
    memset(g_sent, 0, sizeof(g_sent));
    g_sent_count = 0;
    g_defer_calls = 0;
    memset(&g_last_req, 0, sizeof(g_last_req));
    memset(g_last_req_data, 0, sizeof(g_last_req_data));
    g_defer_return = 0;
    g_complete_inline = false;
    memcpy(g_cfg, g_cfg_init, sizeof(g_cfg));
    g_cfg_writes = 0;
    g_cfg_write_max = 0;
    g_cfg_write_fail_at = UINT32_MAX;
    g_cfg_write_error = 0;
    g_cfg_read_error = 0;
    g_reboots = 0;
    g_factory_resets = 0;

    memset(&g_node, 0, sizeof(g_node));
    g_node.id = 0x050101012233ULL;
    g_node.alias = 0xABC;
    g_node.alias_state = LCC_A_CONFIRMED;
    lcc_memconfig_register_node_spaces(&g_node, g_node_spaces, 1);
    lcc_memconfig_register_spaces(g_global_spaces, 2);
    lcc_memconfig_set_hooks(mock_reboot, mock_factory_reset);
}

/* lcc_memconfig_check, returning the error and storing the OK flags. */
static uint8_t g_flags;
static uint16_t check(const uint8_t *pkt, uint8_t len) {
    g_flags = 0xEE;
    return lcc_memconfig_check(&g_node, pkt, len, &g_flags);
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

static void test_check_ok_flags(void) {
    reset_all();
    uint8_t pkt[16];
    uint8_t one = 1;

    uint8_t len = mc_read(pkt, 0, 0xF8, 1);
    assert(check(pkt, len) == 0);
    assert(g_flags ==
           (LCC_DATAGRAM_OK_REPLY_PENDING | LCC_MEMCFG_DEFER_TIMEOUT_EXP));
    len = mc_write(pkt, 0, 0xF8, &one, 1);
    assert(check(pkt, len) == 0);
    assert(g_flags ==
           (LCC_DATAGRAM_OK_REPLY_PENDING | LCC_MEMCFG_DEFER_TIMEOUT_EXP));

    /* Synchronous space: Reply Pending, default timeout. */
    len = mc_read(pkt, 0, 0xFD, 1);
    assert(check(pkt, len) == 0 && g_flags == LCC_DATAGRAM_OK_REPLY_PENDING);
    len = mc_write(pkt, 0, 0xFD, &one, 1);
    assert(check(pkt, len) == 0 && g_flags == LCC_DATAGRAM_OK_REPLY_PENDING);
    uint8_t low_bits[7] = { 0x20, 0x41, 0, 0, 0, 0, 4 };  /* 0xFD, no byte */
    assert(check(low_bits, sizeof(low_bits)) == 0
           && g_flags == LCC_DATAGRAM_OK_REPLY_PENDING);

    /* Options and space info are answered with a reply datagram too. */
    uint8_t info[3] = { 0x20, 0x84, 0xF8 };
    assert(check(info, sizeof(info)) == 0
           && g_flags == LCC_DATAGRAM_OK_REPLY_PENDING);
    uint8_t options[2] = { 0x20, 0x80 };
    assert(check(options, sizeof(options)) == 0
           && g_flags == LCC_DATAGRAM_OK_REPLY_PENDING);
    uint8_t update[2] = { 0x20, 0xA8 };
    assert(check(update, sizeof(update)) == 0 && g_flags == 0);

    printf("  PASS: check accepts valid requests; Reply Pending on every "
           "read/write, longer timeout when deferred\n");
}

static void test_check_rejects_unservable(void) {
    reset_all();
    uint8_t pkt[16];
    uint8_t one = 1;

    /* The case that used to time out: a read of a space we don't have. */
    uint8_t len = mc_read(pkt, 0, 0xEF, 1);
    assert(check(pkt, len) == LCC_ERR_SPACE_UNKNOWN);
    assert(g_flags == 0);
    len = mc_write(pkt, 0, 0xEF, &one, 1);
    assert(check(pkt, len) == LCC_ERR_SPACE_UNKNOWN);
    uint8_t all_mem[7] = { 0x20, 0x42, 0, 0, 0, 0, 1 };  /* 0xFE, unregistered */
    assert(check(all_mem, sizeof(all_mem)) == LCC_ERR_SPACE_UNKNOWN);

    len = mc_write(pkt, 0, 0xFC, &one, 1);       /* read-only space */
    assert(check(pkt, len) == LCC_ERR_WRITE_READ_ONLY);
    len = mc_write(pkt, sizeof(g_cfg), 0xFD, &one, 1);  /* past the end */
    assert(check(pkt, len) == LCC_ERR_OUT_OF_BOUNDS);

    /* Malformed. */
    len = mc_read(pkt, 0, 0xF8, 0);              /* zero count */
    assert(check(pkt, len) == LCC_ERR_INVALID_ARGUMENTS);
    assert(check(pkt, 7) == LCC_ERR_INVALID_ARGUMENTS);  /* no count */
    assert(check(pkt, 5) == LCC_ERR_INVALID_ARGUMENTS);  /* short address */
    len = mc_write(pkt, 0, 0xF8, NULL, 0);       /* no data */
    assert(check(pkt, len) == LCC_ERR_INVALID_ARGUMENTS);
    uint8_t bare[1] = { 0x20 };
    assert(check(bare, sizeof(bare)) == LCC_ERR_INVALID_ARGUMENTS);
    uint8_t short_info[2] = { 0x20, 0x84 };
    assert(check(short_info, sizeof(short_info)) == LCC_ERR_INVALID_ARGUMENTS);

    /* Sub-commands we don't implement, and replies (we're never a client). */
    uint8_t lock[8] = { 0x20, 0x88, 0, 0, 0, 0, 0, 0 };
    assert(check(lock, sizeof(lock)) == LCC_ERR_UNIMPLEMENTED_SUBCMD);
    uint8_t reply[8] = { 0x20, 0x50, 0, 0, 0, 0, 0xFD, 0 };
    assert(check(reply, sizeof(reply)) == LCC_ERR_UNIMPLEMENTED_SUBCMD);
    uint8_t reserved_read[8] = { 0x20, 0x44, 0, 0, 0, 0, 0xFD, 1 };
    assert(check(reserved_read, sizeof(reserved_read))
           == LCC_ERR_UNIMPLEMENTED_SUBCMD);

    assert(g_sent_count == 0);
    printf("  PASS: check rejects unknown/read-only spaces, bad requests, "
           "unknown commands\n");
}

static void test_reboot_and_factory_reset(void) {
    reset_all();
    uint8_t reboot[2] = { 0x20, 0xA9 };
    assert(check(reboot, sizeof(reboot)) == 0);
    lcc_memconfig_handle(&g_node, 0x123, reboot, sizeof(reboot));
    assert(g_reboots == 1);

    uint8_t reset[8] = { 0x20, 0xAA, 0x05, 0x01, 0x01, 0x01, 0x22, 0x33 };
    assert(check(reset, sizeof(reset)) == 0);
    lcc_memconfig_handle(&g_node, 0x123, reset, sizeof(reset));
    assert(g_factory_resets == 1);

    reset[7] = 0x34;                             /* someone else's node ID */
    assert(check(reset, sizeof(reset)) == LCC_ERR_INVALID_ARGUMENTS);
    lcc_memconfig_handle(&g_node, 0x123, reset, sizeof(reset));
    assert(g_factory_resets == 1);
    assert(check(reset, 7) == LCC_ERR_INVALID_ARGUMENTS);

    lcc_memconfig_set_hooks(NULL, NULL);
    reset[7] = 0x33;
    assert(check(reboot, sizeof(reboot)) == LCC_ERR_UNIMPLEMENTED_SUBCMD);
    assert(check(reset, sizeof(reset)) == LCC_ERR_UNIMPLEMENTED_SUBCMD);

    assert(g_sent_count == 0);                   /* OK comes from lcc_datagram */
    printf("  PASS: reboot/factory reset need hooks and a matching node ID\n");
}

static void test_sync_write(void) {
    reset_all();
    uint8_t pkt[16];
    uint8_t vals[2] = { 0xA1, 0xA2 };
    uint8_t len = mc_write(pkt, 2, 0xFD, vals, 2);
    assert(check(pkt, len) == 0);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    assert(g_cfg_writes == 1);
    assert(g_cfg[2] == 0xA1 && g_cfg[3] == 0xA2);
    const uint8_t want[] = { 0x20, 0x10, 0, 0, 0, 2, 0xFD };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));

    uint8_t low_bits[8] = { 0x20, 0x01, 0, 0, 0, 5, 0xB1, 0xB2 };  /* 0xFD */
    lcc_memconfig_handle(&g_node, 0x123, low_bits, sizeof(low_bits));
    const uint8_t want_low[] = { 0x20, 0x11, 0, 0, 0, 5 };
    assert(g_sent_count == 2);
    assert_bytes(&g_sent[1], want_low, sizeof(want_low));
    assert(g_cfg[5] == 0xB1 && g_cfg[6] == 0xB2);
    printf("  PASS: synchronous write lands and sends Write Reply OK\n");
}

static void test_get_options(void) {
    reset_all();
    uint8_t options[2] = { 0x20, 0x80 };

    /* Fixtures: 0xF8 on the node, 0xFD and a read-only 0xFC globally.
     * Unaligned reads + writes + 0xFC readable; lengths 1/2/4/arbitrary;
     * spaces 0xF8..0xFC with 0xFD-0xFF assumed. */
    lcc_memconfig_handle(&g_node, 0x123, options, sizeof(options));
    const uint8_t want[] = { 0x20, 0x82, 0x68, 0x00, 0xE2, 0xFC, 0xF8 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));

    /* A writable 0xFB, and no 0xFC. */
    static const lcc_memspace_handler_t with_fb[] = {
        { .space = 0xFD, .size = sizeof(g_cfg), .read = cfg_read,
          .write = cfg_write },
        { .space = 0xFB, .size = 4, .read = cfg_read, .write = cfg_write },
    };
    lcc_memconfig_register_spaces(with_fb, 2);
    lcc_memconfig_handle(&g_node, 0x123, options, sizeof(options));
    const uint8_t want_fb[] = { 0x20, 0x82, 0x66, 0x00, 0xE2, 0xFB, 0xF8 };
    assert_bytes(&g_sent[1], want_fb, sizeof(want_fb));

    /* Only the assumed high spaces. */
    lcc_memconfig_register_node_spaces(&g_node, NULL, 0);
    lcc_memconfig_register_spaces(with_fb, 1);
    lcc_memconfig_handle(&g_node, 0x123, options, sizeof(options));
    const uint8_t want_high[] = { 0x20, 0x82, 0x60, 0x00, 0xE2, 0xFF, 0xFD };
    assert_bytes(&g_sent[2], want_high, sizeof(want_high));
    printf("  PASS: Get Configuration Options reports commands and spaces\n");
}

static void test_sync_write_failures(void) {
    reset_all();
    uint8_t pkt[24];
    uint8_t vals[4] = { 0xC1, 0xC2, 0xC3, 0xC4 };

    /* Handler refuses the address: Write Reply Failed with its code. */
    g_cfg_write_fail_at = 4;
    g_cfg_write_error = LCC_ERR_WRITE_READ_ONLY;
    uint8_t len = mc_write(pkt, 4, 0xFD, vals, 1);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    const uint8_t refused[] = { 0x20, 0x18, 0, 0, 0, 4, 0xFD, 0x10, 0x83 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], refused, sizeof(refused));

    /* Handler stalls with no reason: still a failure, not a hang. */
    g_cfg_write_error = 0;
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    const uint8_t stalled[] = { 0x20, 0x18, 0, 0, 0, 4, 0xFD, 0x10, 0x00 };
    assert(g_sent_count == 2);
    assert_bytes(&g_sent[1], stalled, sizeof(stalled));

    /* Straddles the end: the in-bounds bytes land, the reply fails. */
    reset_all();
    len = mc_write(pkt, sizeof(g_cfg) - 2, 0xFD, vals, 4);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    assert(g_cfg[14] == 0xC1 && g_cfg[15] == 0xC2);
    const uint8_t straddle[] = { 0x20, 0x18, 0, 0, 0, 14, 0xFD, 0x10, 0x82 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], straddle, sizeof(straddle));
    printf("  PASS: synchronous write failures send Write Reply Failed\n");
}

static void test_sync_write_offers_the_rest(void) {
    reset_all();
    g_cfg_write_max = 1;                         /* one byte per call */
    uint8_t pkt[24];
    uint8_t vals[3] = { 0xD1, 0xD2, 0xD3 };
    uint8_t len = mc_write(pkt, 8, 0xFD, vals, 3);
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    assert(g_cfg_writes == 3);
    assert(g_cfg[8] == 0xD1 && g_cfg[9] == 0xD2 && g_cfg[10] == 0xD3);
    const uint8_t want[] = { 0x20, 0x10, 0, 0, 0, 8, 0xFD };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));

    /* A failure partway reports it, after the bytes before it landed. */
    reset_all();
    g_cfg_write_max = 1;
    g_cfg_write_fail_at = 9;
    g_cfg_write_error = LCC_ERR_INVALID_ARGUMENTS;
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    assert(g_cfg[8] == 0xD1 && g_cfg[9] == 0x00);
    const uint8_t failed[] = { 0x20, 0x18, 0, 0, 0, 8, 0xFD, 0x10, 0x80 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], failed, sizeof(failed));
    printf("  PASS: a handler taking part of a write is offered the rest\n");
}

/* A space that answers addresses 0-3 itself and passes the rest on. */
static uint16_t mixed_read(lcc_node_t *node, uint32_t addr, uint16_t count,
                           uint8_t *out, uint16_t *error) {
    (void)node;
    if (addr >= 4) {
        *error = LCC_MEMCFG_DEFER;
        return 0;
    }
    memcpy(out, &g_cfg[addr], count);
    return count;
}

static void test_read_handler_passes_on_to_defer(void) {
    reset_all();
    static const lcc_memspace_handler_t mixed[] = {
        { .space = 0xF8, .size = 16, .read = mixed_read,
          .write = cfg_write, .defer = mock_defer },
    };
    lcc_node_t node = g_node;
    lcc_memconfig_register_node_spaces(&node, mixed, 1);

    /* Answered at once, without taking the deferred slot. */
    uint8_t pkt[16];
    uint8_t len = mc_read(pkt, 1, 0xF8, 1);
    lcc_memconfig_handle(&node, 0x123, pkt, len);
    const uint8_t now[] = { 0x20, 0x50, 0, 0, 0, 1, 0xF8, 0x11 };
    assert(g_defer_calls == 0);
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], now, sizeof(now));
    assert(!lcc_memconfig_deferred_pending());

    /* Passed on, with the requested count. */
    len = mc_read(pkt, 5, 0xF8, 2);
    lcc_memconfig_handle(&node, 0x123, pkt, len);
    assert(g_defer_calls == 1);
    assert(g_last_req.addr == 5 && g_last_req.count == 2);
    assert(g_sent_count == 1);
    uint8_t v = 0x42;
    lcc_memconfig_complete(0, &v, 1);
    lcc_memconfig_flush_deferred();
    const uint8_t later[] = { 0x20, 0x50, 0, 0, 0, 5, 0xF8, 0x42 };
    assert(g_sent_count == 2);
    assert_bytes(&g_sent[1], later, sizeof(later));

    /* Writes still go to the write handler. */
    uint8_t data = 0x77;
    len = mc_write(pkt, 6, 0xF8, &data, 1);
    lcc_memconfig_handle(&node, 0x123, pkt, len);
    assert(g_defer_calls == 1);
    assert(g_cfg[6] == 0x77);
    assert(g_sent_count == 3);

    /* With no defer hook, passing on fails the read. */
    static const lcc_memspace_handler_t no_defer[] = {
        { .space = 0xF8, .size = 16, .read = mixed_read },
    };
    lcc_memconfig_register_node_spaces(&node, no_defer, 1);
    len = mc_read(pkt, 5, 0xF8, 1);
    lcc_memconfig_handle(&node, 0x123, pkt, len);
    const uint8_t failed[] = { 0x20, 0x58, 0, 0, 0, 5, 0xF8, 0x10, 0x00 };
    assert(g_sent_count == 4);
    assert_bytes(&g_sent[3], failed, sizeof(failed));
    assert(!lcc_memconfig_deferred_pending());

    printf("  PASS: a read handler can pass requests on to defer\n");
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

static void test_sync_read_error_replies_failed(void) {
    reset_all();
    g_cfg_read_error = LCC_ERR_UNIMPLEMENTED_CMD;
    uint8_t low_bits[7] = { 0x20, 0x41, 0, 0, 0, 1, 2 };  /* 0xFD @1, 2 bytes */
    lcc_memconfig_handle(&g_node, 0x123, low_bits, sizeof(low_bits));
    const uint8_t want[] = { 0x20, 0x59, 0, 0, 0, 1, 0x10, 0x42 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));

    uint8_t pkt[16];
    uint8_t len = mc_read(pkt, 3, 0xFD, 1);      /* explicit space byte */
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    const uint8_t want_explicit[] = { 0x20, 0x58, 0, 0, 0, 3, 0xFD, 0x10, 0x42 };
    assert(g_sent_count == 2);
    assert_bytes(&g_sent[1], want_explicit, sizeof(want_explicit));
    printf("  PASS: synchronous read error becomes Read Reply Failed\n");
}

static void test_sync_read_past_end_is_short(void) {
    reset_all();
    uint8_t pkt[16];
    uint8_t len = mc_read(pkt, sizeof(g_cfg) - 1, 0xFD, 4);  /* last byte + 3 */
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    const uint8_t want[] = { 0x20, 0x50, 0, 0, 0, 15, 0xFD, 0x00 };
    assert(g_sent_count == 1);
    assert_bytes(&g_sent[0], want, sizeof(want));

    len = mc_read(pkt, sizeof(g_cfg), 0xFD, 4);  /* wholly past the end */
    lcc_memconfig_handle(&g_node, 0x123, pkt, len);
    const uint8_t empty[] = { 0x20, 0x50, 0, 0, 0, 16, 0xFD };
    assert(g_sent_count == 2);
    assert_bytes(&g_sent[1], empty, sizeof(empty));
    printf("  PASS: synchronous read past the end gets a short reply\n");
}

int main(void) {
    printf("test_lcc_memconfig:\n");
    test_check_ok_flags();
    test_check_rejects_unservable();
    test_reboot_and_factory_reset();
    test_sync_write();
    test_get_options();
    test_sync_write_failures();
    test_sync_write_offers_the_rest();
    test_read_handler_passes_on_to_defer();
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
    test_sync_read_error_replies_failed();
    test_sync_read_past_end_is_short();
    printf("All LCC memconfig tests passed.\n");
    return 0;
}
