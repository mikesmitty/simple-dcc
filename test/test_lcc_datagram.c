#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "lcc/lcc_datagram.h"
#include "lcc/lcc_memconfig.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_node.h"

/* Real lcc_datagram.c + lcc_memconfig.c; only the wire is mocked. */

#define OUR_ALIAS   0xABC
#define PEER_ALIAS  0x123
#define OTHER_ALIAS 0x456

static lcc_frame_t g_tx[16];
static int         g_tx_count;

bool lcc_if_tx(const lcc_frame_t *frame) {
    assert(g_tx_count < (int)(sizeof(g_tx) / sizeof(g_tx[0])));
    g_tx[g_tx_count++] = *frame;
    return true;
}

/* ---- spaces ------------------------------------------------------------- */

static uint8_t g_cfg[16];
static uint16_t cfg_read(lcc_node_t *node, uint32_t addr, uint16_t count,
                         uint8_t *out, uint16_t *error) {
    (void)node; (void)error;
    memcpy(out, &g_cfg[addr], count);
    return count;
}
static uint16_t cfg_write(lcc_node_t *node, uint32_t addr, uint16_t count,
                          const uint8_t *in) {
    (void)node;
    memcpy(&g_cfg[addr], in, count);
    return count;
}
static const lcc_memspace_handler_t g_global_spaces[] = {
    { .space = 0xFD, .size = sizeof(g_cfg), .read = cfg_read,
      .write = cfg_write },
};

static int g_defer_calls;
static uint16_t mock_defer(lcc_node_t *node,
                           const lcc_memconfig_deferred_t *req) {
    (void)node; (void)req;
    g_defer_calls++;
    return 0;
}
static const lcc_memspace_handler_t g_node_spaces[] = {
    { .space = 0xF8, .size = 1024, .defer = mock_defer },
};

static lcc_node_t g_node;

static void reset_all(void) {
    memset(g_tx, 0, sizeof(g_tx));
    g_tx_count = 0;
    memset(g_cfg, 0, sizeof(g_cfg));
    g_defer_calls = 0;
    /* Drain any deferred request a previous test left in flight. */
    lcc_memconfig_complete(LCC_ERR_TEMPORARY, NULL, 0);
    lcc_memconfig_flush_deferred();
    g_tx_count = 0;

    memset(&g_node, 0, sizeof(g_node));
    g_node.id = 0x050101012233ULL;
    g_node.alias = OUR_ALIAS;
    g_node.alias_state = LCC_A_CONFIRMED;
    lcc_memconfig_register_node_spaces(&g_node, g_node_spaces, 1);
    lcc_memconfig_register_spaces(g_global_spaces, 1);
}

/* ---- frame helpers ------------------------------------------------------ */

static void rx(uint8_t ftype, uint16_t src, const uint8_t *data, uint8_t n) {
    lcc_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id  = lcc_frame_datagram_id(ftype, OUR_ALIAS, src);
    f.dlc = n;
    if (n) memcpy(f.data, data, n);
    lcc_datagram_handle_frame(&g_node, &f);
}

static uint16_t tx_mti(int i) { return lcc_get_mti(g_tx[i].id); }

static uint16_t tx_addressed_dst(int i) {
    return (uint16_t)(((g_tx[i].data[0] & 0x0F) << 8) | g_tx[i].data[1]);
}

static void assert_rejected(int i, uint16_t dst, uint16_t error) {
    assert(tx_mti(i) == LCC_MTI_DATAGRAM_REJECTED);
    assert(tx_addressed_dst(i) == dst);
    assert(g_tx[i].dlc == 4);
    assert(((g_tx[i].data[2] << 8) | g_tx[i].data[3]) == error);
}

static void assert_ok(int i, uint16_t dst, int flags) {
    assert(tx_mti(i) == LCC_MTI_DATAGRAM_OK);
    assert(tx_addressed_dst(i) == dst);
    if (flags < 0) {
        assert(g_tx[i].dlc == 2);
    } else {
        assert(g_tx[i].dlc == 3);
        assert(g_tx[i].data[2] == flags);
    }
}

static bool tx_is_datagram(int i) {
    uint8_t t = lcc_get_can_frame_type(g_tx[i].id);
    return t >= LCC_FRAME_DATAGRAM_ONE && t <= LCC_FRAME_DATAGRAM_FINAL;
}

/* ---- tests -------------------------------------------------------------- */

static void test_unknown_space_read_is_rejected(void) {
    reset_all();
    const uint8_t read[8] = { 0x20, 0x40, 0, 0, 0, 0, 0xEF, 1 };
    rx(LCC_FRAME_DATAGRAM_ONE, PEER_ALIAS, read, sizeof(read));

    assert(g_tx_count == 1);                     /* no OK, no reply */
    assert_rejected(0, PEER_ALIAS, LCC_ERR_SPACE_UNKNOWN);
    printf("  PASS: read of an unknown space gets Datagram Rejected 0x1081\n");
}

static void test_known_space_read_is_acked_then_answered(void) {
    reset_all();
    g_cfg[2] = 0x77;
    const uint8_t read[7] = { 0x20, 0x41, 0, 0, 0, 2, 1 };  /* 0xFD @2, 1 */
    rx(LCC_FRAME_DATAGRAM_ONE, PEER_ALIAS, read, sizeof(read));

    assert(g_tx_count == 2);
    assert_ok(0, PEER_ALIAS, -1);
    assert(tx_is_datagram(1));
    const uint8_t want[7] = { 0x20, 0x51, 0, 0, 0, 2, 0x77 };
    assert(g_tx[1].dlc == sizeof(want));
    assert(memcmp(g_tx[1].data, want, sizeof(want)) == 0);
    printf("  PASS: read of a known space is OK'd, then answered\n");
}

static void test_deferred_read_ok_has_reply_pending(void) {
    reset_all();
    const uint8_t read[8] = { 0x20, 0x40, 0, 0, 0, 0, 0xF8, 1 };
    rx(LCC_FRAME_DATAGRAM_ONE, PEER_ALIAS, read, sizeof(read));

    assert(g_tx_count == 1);
    assert_ok(0, PEER_ALIAS,
              LCC_DATAGRAM_OK_REPLY_PENDING | LCC_MEMCFG_DEFER_TIMEOUT_EXP);
    assert(g_defer_calls == 1);
    printf("  PASS: deferred read is OK'd with Reply Pending\n");
}

static void test_unknown_protocol_is_permanent(void) {
    reset_all();
    const uint8_t dg[2] = { 0x30, 0x00 };
    rx(LCC_FRAME_DATAGRAM_ONE, PEER_ALIAS, dg, sizeof(dg));

    assert(g_tx_count == 1);
    assert_rejected(0, PEER_ALIAS, LCC_ERR_UNIMPLEMENTED_CMD);
    printf("  PASS: unknown datagram protocol is rejected permanently\n");
}

static void test_empty_datagram_is_rejected(void) {
    reset_all();
    rx(LCC_FRAME_DATAGRAM_ONE, PEER_ALIAS, NULL, 0);

    assert(g_tx_count == 1);
    assert_rejected(0, PEER_ALIAS, LCC_ERR_PERMANENT);
    printf("  PASS: empty datagram is rejected\n");
}

static void test_final_without_first_is_out_of_order(void) {
    reset_all();
    /* Looks like a complete read on its own; it must not be processed. */
    const uint8_t tail[7] = { 0x20, 0x41, 0, 0, 0, 0, 1 };
    rx(LCC_FRAME_DATAGRAM_FINAL, PEER_ALIAS, tail, sizeof(tail));

    assert(g_tx_count == 1);
    assert_rejected(0, PEER_ALIAS, LCC_ERR_OUT_OF_ORDER);
    printf("  PASS: FINAL with no FIRST is rejected as out of order\n");
}

static void test_middle_without_first_is_out_of_order(void) {
    reset_all();
    const uint8_t mid[8] = { 0 };
    rx(LCC_FRAME_DATAGRAM_MIDDLE, PEER_ALIAS, mid, sizeof(mid));

    assert(g_tx_count == 1);
    assert_rejected(0, PEER_ALIAS, LCC_ERR_OUT_OF_ORDER);
    printf("  PASS: MIDDLE with no FIRST is rejected as out of order\n");
}

static void test_multi_frame_write(void) {
    reset_all();
    /* Write 0xFD @0 with 4 bytes: 6 header + 4 data, split 8 + 2. */
    const uint8_t first[8] = { 0x20, 0x01, 0, 0, 0, 0, 0xA1, 0xA2 };
    const uint8_t final[2] = { 0xA3, 0xA4 };
    rx(LCC_FRAME_DATAGRAM_FIRST, PEER_ALIAS, first, sizeof(first));
    assert(g_tx_count == 0);
    rx(LCC_FRAME_DATAGRAM_FINAL, PEER_ALIAS, final, sizeof(final));

    assert(g_tx_count == 1);
    assert_ok(0, PEER_ALIAS, -1);
    const uint8_t want[4] = { 0xA1, 0xA2, 0xA3, 0xA4 };
    assert(memcmp(g_cfg, want, sizeof(want)) == 0);
    printf("  PASS: multi-frame write is reassembled and applied\n");
}

static void test_interleaved_sender_is_out_of_order(void) {
    reset_all();
    const uint8_t first[8] = { 0x20, 0x01, 0, 0, 0, 0, 0xB1, 0xB2 };
    const uint8_t stray[8] = { 0 };
    const uint8_t final[1] = { 0xB3 };
    rx(LCC_FRAME_DATAGRAM_FIRST, PEER_ALIAS, first, sizeof(first));
    rx(LCC_FRAME_DATAGRAM_MIDDLE, OTHER_ALIAS, stray, sizeof(stray));
    assert(g_tx_count == 1);
    assert_rejected(0, OTHER_ALIAS, LCC_ERR_OUT_OF_ORDER);

    /* The first sender's datagram is untouched. */
    rx(LCC_FRAME_DATAGRAM_FINAL, PEER_ALIAS, final, sizeof(final));
    assert(g_tx_count == 2);
    assert_ok(1, PEER_ALIAS, -1);
    assert(g_cfg[0] == 0xB1 && g_cfg[1] == 0xB2 && g_cfg[2] == 0xB3);
    printf("  PASS: another sender's stray frame doesn't disturb a datagram\n");
}

static void test_overflow_is_permanent(void) {
    reset_all();
    uint8_t chunk[8];
    memset(chunk, 0, sizeof(chunk));
    chunk[0] = 0x20;
    chunk[1] = 0x01;
    rx(LCC_FRAME_DATAGRAM_FIRST, PEER_ALIAS, chunk, 8);
    for (int i = 0; i < 8; i++)                  /* 8 + 64 = 72, the max */
        rx(LCC_FRAME_DATAGRAM_MIDDLE, PEER_ALIAS, chunk, 8);
    assert(g_tx_count == 0);
    rx(LCC_FRAME_DATAGRAM_MIDDLE, PEER_ALIAS, chunk, 1);

    assert(g_tx_count == 1);
    assert_rejected(0, PEER_ALIAS, LCC_ERR_PERMANENT);

    /* Buffer was released: the trailing FINAL is now an orphan. */
    rx(LCC_FRAME_DATAGRAM_FINAL, PEER_ALIAS, chunk, 1);
    assert(g_tx_count == 2);
    assert_rejected(1, PEER_ALIAS, LCC_ERR_OUT_OF_ORDER);
    printf("  PASS: datagram over 72 bytes is rejected permanently\n");
}

int main(void) {
    printf("test_lcc_datagram:\n");
    test_unknown_space_read_is_rejected();
    test_known_space_read_is_acked_then_answered();
    test_deferred_read_ok_has_reply_pending();
    test_unknown_protocol_is_permanent();
    test_empty_datagram_is_rejected();
    test_final_without_first_is_out_of_order();
    test_middle_without_first_is_out_of_order();
    test_multi_frame_write();
    test_interleaved_sender_is_out_of_order();
    test_overflow_is_permanent();
    printf("All LCC datagram tests passed.\n");
    return 0;
}
