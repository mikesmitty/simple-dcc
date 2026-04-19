#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "lcc/lcc_traction.h"
#include "lcc/lcc_node.h"
#include "lcc/lcc_frame.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_float16.h"

/* ---- DCC hook capture ---------------------------------------------------- */

typedef struct {
    uint16_t addr;
    uint8_t  step;
    bool     forward;
    int      calls;
} throttle_rec_t;

typedef struct {
    uint16_t addr;
    uint16_t fn;
    bool     on;
    int      calls;
} function_rec_t;

typedef struct {
    uint16_t addr;
    int      calls;
} estop_rec_t;

static throttle_rec_t last_throttle;
static function_rec_t last_function;
static estop_rec_t    last_estop;

static void mock_set_throttle(uint16_t addr, uint8_t step, bool forward) {
    last_throttle.addr = addr;
    last_throttle.step = step;
    last_throttle.forward = forward;
    last_throttle.calls++;
}
static void mock_set_function(uint16_t addr, uint16_t fn, bool on) {
    last_function.addr = addr;
    last_function.fn = fn;
    last_function.on = on;
    last_function.calls++;
}
static void mock_emergency_stop(uint16_t addr) {
    last_estop.addr = addr;
    last_estop.calls++;
}

/* ---- lcc_node / lcc_if mocks -------------------------------------------- */
/* lcc_traction.c calls lcc_node_send_addressed() to emit replies and
 * lcc_node_count()/lcc_node_at() when forwarding to consist slaves. We
 * stub out node send and keep the registry empty so forwarding is a no-op
 * unless a test explicitly populates it. */

typedef struct {
    uint16_t mti;
    uint16_t dst_alias;
    uint8_t  payload[32];
    uint16_t payload_len;
    int      calls;
} reply_rec_t;

static reply_rec_t last_reply;

void lcc_node_send_addressed(const lcc_node_t *node, uint16_t mti,
                             uint16_t dst_alias,
                             const uint8_t *payload, uint16_t payload_len) {
    (void)node;
    last_reply.mti = mti;
    last_reply.dst_alias = dst_alias;
    last_reply.payload_len = payload_len < sizeof(last_reply.payload)
                           ? payload_len : (uint16_t)sizeof(last_reply.payload);
    memcpy(last_reply.payload, payload, last_reply.payload_len);
    last_reply.calls++;
}

void lcc_node_send_global(const lcc_node_t *node, uint16_t mti,
                          const uint8_t *data, uint8_t dlc) {
    (void)node; (void)mti; (void)data; (void)dlc;
}

/* Consist forwarding walks the registry; keep it empty for the SET_* /
 * QUERY tests so no loopback frames are synthesized. */
static lcc_node_t *g_mock_registry[4];
static int         g_mock_count;

int lcc_node_count(void) { return g_mock_count; }
lcc_node_t *lcc_node_at(int idx) {
    if (idx < 0 || idx >= g_mock_count) return NULL;
    return g_mock_registry[idx];
}

/* ---- helpers ------------------------------------------------------------ */

static void reset_all(void) {
    memset(&last_throttle, 0, sizeof(last_throttle));
    memset(&last_function, 0, sizeof(last_function));
    memset(&last_estop, 0, sizeof(last_estop));
    memset(&last_reply, 0, sizeof(last_reply));
    memset(g_mock_registry, 0, sizeof(g_mock_registry));
    g_mock_count = 0;

    lcc_traction_hooks_t hooks = {
        .set_throttle   = mock_set_throttle,
        .set_function   = mock_set_function,
        .emergency_stop = mock_emergency_stop,
    };
    lcc_traction_set_hooks(&hooks);
}

/* Build a single-frame addressed 0x05EB command.
 * cmd_payload is [cmd_byte, ...args]. */
static lcc_frame_t make_tc_frame(uint16_t src_alias, uint16_t dst_alias,
                                 const uint8_t *cmd_payload, uint8_t len) {
    lcc_frame_t f;
    f.id = lcc_can_id(LCC_MTI_TRACTION_CONTROL_COMMAND, src_alias);
    lcc_frame_build_addressed_hdr(f.data, dst_alias, 0);
    memcpy(&f.data[2], cmd_payload, len);
    f.dlc = (uint8_t)(2 + len);
    return f;
}

static void init_node_with_train(lcc_node_t *n, lcc_train_state_t *t,
                                  uint16_t dcc_addr, bool is_long,
                                  uint16_t alias) {
    lcc_train_state_init(t, dcc_addr, is_long);
    memset(n, 0, sizeof(*n));
    n->id = 0x060100000000ULL | dcc_addr;
    n->alias = alias;
    n->alias_state = LCC_A_CONFIRMED;
    n->role = LCC_ROLE_TRAIN;
    n->train = t;
}

/* ---- SET_SPEED_DIRECTION ----------------------------------------------- */

static void test_set_speed_forward(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0x456);

    lcc_float16_t v = lcc_float16_from_float(30.0f);
    uint8_t cmd[3] = { LCC_TRAC_SET_SPEED_DIR, (uint8_t)(v >> 8), (uint8_t)v };
    lcc_frame_t f = make_tc_frame(0x123, 0x456, cmd, sizeof(cmd));
    lcc_traction_handle_frame(&n, &f);

    assert(last_throttle.calls == 1);
    assert(last_throttle.addr == 42);
    assert(last_throttle.forward == true);
    assert(last_throttle.step >= 2 && last_throttle.step <= 127);
    assert(t.last_speed_f16 == v);
    assert(t.emergency == false);

    printf("  PASS: set_speed forward\n");
}

static void test_set_speed_reverse(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 7, false, 0xA11);

    /* Negative velocity → reverse direction. */
    lcc_float16_t v = lcc_float16_from_float(-20.0f);
    uint8_t cmd[3] = { LCC_TRAC_SET_SPEED_DIR, (uint8_t)(v >> 8), (uint8_t)v };
    lcc_frame_t f = make_tc_frame(0x123, 0xA11, cmd, sizeof(cmd));
    lcc_traction_handle_frame(&n, &f);

    assert(last_throttle.calls == 1);
    assert(last_throttle.forward == false);
    assert(last_throttle.step >= 2);

    printf("  PASS: set_speed reverse\n");
}

static void test_set_speed_stop(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 3, false, 0x222);

    uint8_t cmd[3] = { LCC_TRAC_SET_SPEED_DIR, 0x00, 0x00 };  /* +0 */
    lcc_frame_t f = make_tc_frame(0x123, 0x222, cmd, sizeof(cmd));
    lcc_traction_handle_frame(&n, &f);

    assert(last_throttle.calls == 1);
    assert(last_throttle.step == 0);
    assert(last_throttle.forward == true);

    printf("  PASS: set_speed stop\n");
}

static void test_set_speed_nan_estop(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 3, false, 0x222);

    /* Float16 NaN — encoded with exp=0x1F + non-zero mantissa. */
    uint8_t cmd[3] = { LCC_TRAC_SET_SPEED_DIR, 0x7C, 0x01 };
    lcc_frame_t f = make_tc_frame(0x123, 0x222, cmd, sizeof(cmd));
    lcc_traction_handle_frame(&n, &f);

    assert(last_throttle.calls == 1);
    assert(last_throttle.step == 1);  /* DCC estop step */

    printf("  PASS: set_speed NaN → estop step\n");
}

/* ---- SET_FN ------------------------------------------------------------- */

static void test_set_fn_basic(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 3, false, 0x222);

    /* SET_FN fn=5 on (value=1). */
    uint8_t cmd[6] = { LCC_TRAC_SET_FN, 0x00, 0x00, 0x05, 0x00, 0x01 };
    lcc_frame_t f = make_tc_frame(0x123, 0x222, cmd, sizeof(cmd));
    lcc_traction_handle_frame(&n, &f);

    assert(last_function.calls == 1);
    assert(last_function.addr == 3);
    assert(last_function.fn == 5);
    assert(last_function.on == true);
    assert((t.fn_state & (1ULL << 5)) != 0);

    /* SET_FN fn=5 off. */
    cmd[5] = 0;
    f = make_tc_frame(0x123, 0x222, cmd, sizeof(cmd));
    lcc_traction_handle_frame(&n, &f);

    assert(last_function.calls == 2);
    assert(last_function.on == false);
    assert((t.fn_state & (1ULL << 5)) == 0);

    printf("  PASS: set_fn basic on/off\n");
}

static void test_set_fn_high_range(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 3, false, 0x222);

    /* F64 on — exercises the high-range fn_state_hi path. */
    uint8_t cmd[6] = { LCC_TRAC_SET_FN, 0x00, 0x00, 64, 0x00, 0x01 };
    lcc_frame_t f = make_tc_frame(0x123, 0x222, cmd, sizeof(cmd));
    lcc_traction_handle_frame(&n, &f);

    assert(last_function.calls == 1);
    assert(last_function.fn == 64);
    assert(t.fn_state_hi & 0x01);

    /* F69 is out of range and should be dropped entirely. */
    cmd[3] = 69;
    f = make_tc_frame(0x123, 0x222, cmd, sizeof(cmd));
    lcc_traction_handle_frame(&n, &f);
    assert(last_function.calls == 1);  /* no new call */

    printf("  PASS: set_fn F64 + out-of-range\n");
}

/* ---- EMERGENCY_STOP ----------------------------------------------------- */

static void test_emergency_stop(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0x456);

    uint8_t cmd[1] = { LCC_TRAC_EMERGENCY_STOP };
    lcc_frame_t f = make_tc_frame(0x123, 0x456, cmd, sizeof(cmd));
    lcc_traction_handle_frame(&n, &f);

    assert(last_estop.calls == 1);
    assert(last_estop.addr == 42);
    assert(t.emergency == true);

    printf("  PASS: emergency_stop\n");
}

/* ---- QUERY_SPEEDS / QUERY_FUNCTION replies ----------------------------- */

static void test_query_speeds_reply(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 3, false, 0x222);

    /* Prime with a known speed, then query. */
    lcc_float16_t v = lcc_float16_from_float(42.0f);
    uint8_t set_cmd[3] = { LCC_TRAC_SET_SPEED_DIR, (uint8_t)(v >> 8), (uint8_t)v };
    lcc_frame_t f = make_tc_frame(0x123, 0x222, set_cmd, sizeof(set_cmd));
    lcc_traction_handle_frame(&n, &f);

    uint8_t q_cmd[1] = { LCC_TRAC_QUERY_SPEEDS };
    f = make_tc_frame(0x123, 0x222, q_cmd, sizeof(q_cmd));
    lcc_traction_handle_frame(&n, &f);

    assert(last_reply.calls == 1);
    assert(last_reply.mti == LCC_MTI_TRACTION_CONTROL_REPLY);
    assert(last_reply.dst_alias == 0x123);
    assert(last_reply.payload_len == 8);
    assert(last_reply.payload[0] == LCC_TRAC_QUERY_SPEEDS);
    /* set_f16 echoes back verbatim at bytes 1..2. */
    assert(last_reply.payload[1] == (uint8_t)(v >> 8));
    assert(last_reply.payload[2] == (uint8_t)v);
    /* status bit 0x80 clear — not in emergency. */
    assert((last_reply.payload[3] & 0x80) == 0);

    printf("  PASS: query_speeds reply\n");
}

static void test_query_function_reply(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 3, false, 0x222);

    /* Turn F3 on, then query it back. */
    uint8_t set_cmd[6] = { LCC_TRAC_SET_FN, 0x00, 0x00, 0x03, 0x00, 0x01 };
    lcc_frame_t f = make_tc_frame(0x123, 0x222, set_cmd, sizeof(set_cmd));
    lcc_traction_handle_frame(&n, &f);

    uint8_t q_cmd[4] = { LCC_TRAC_QUERY_FUNCTION, 0x00, 0x00, 0x03 };
    f = make_tc_frame(0x123, 0x222, q_cmd, sizeof(q_cmd));
    lcc_traction_handle_frame(&n, &f);

    assert(last_reply.calls == 1);
    assert(last_reply.mti == LCC_MTI_TRACTION_CONTROL_REPLY);
    assert(last_reply.payload_len == 6);
    assert(last_reply.payload[0] == LCC_TRAC_QUERY_FUNCTION);
    assert(last_reply.payload[3] == 0x03);    /* fn address echo */
    assert(last_reply.payload[4] == 0x00);
    assert(last_reply.payload[5] == 0x01);    /* value = on */

    printf("  PASS: query_function reply\n");
}

/* ---- CONTROLLER_CFG (ASSIGN / QUERY / RELEASE) ------------------------- */

static void test_controller_assign_query_release(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0x456);

    /* ASSIGN: 0x20, 0x01, flags=0, 6-byte NID. */
    uint64_t owner = 0x02010D000001ULL;
    uint8_t assign[9] = { LCC_TRAC_CONTROLLER_CFG, LCC_CTRLREQ_ASSIGN, 0x00 };
    lcc_node_id_to_buf(owner, &assign[3]);
    lcc_frame_t f = make_tc_frame(0x123, 0x456, assign, sizeof(assign));
    lcc_traction_handle_frame(&n, &f);

    assert(t.controller_owner == owner);
    assert(last_reply.calls == 1);
    assert(last_reply.payload[0] == LCC_TRAC_CONTROLLER_CFG);
    assert(last_reply.payload[1] == LCC_CTRLREQ_ASSIGN);
    assert(last_reply.payload[2] == LCC_CTRLRESP_OK);

    /* QUERY: expect the owner NID in the reply. */
    reset_all();
    init_node_with_train(&n, &t, 42, false, 0x456);
    t.controller_owner = owner;
    uint8_t query[2] = { LCC_TRAC_CONTROLLER_CFG, LCC_CTRLREQ_QUERY };
    f = make_tc_frame(0x123, 0x456, query, sizeof(query));
    lcc_traction_handle_frame(&n, &f);

    assert(last_reply.calls == 1);
    assert(last_reply.payload_len == 9);
    assert(last_reply.payload[1] == LCC_CTRLREQ_QUERY);
    assert(lcc_node_id_from_buf(&last_reply.payload[3]) == owner);

    /* RELEASE by the current owner clears the slot. */
    uint8_t release[9] = { LCC_TRAC_CONTROLLER_CFG, LCC_CTRLREQ_RELEASE, 0x00 };
    lcc_node_id_to_buf(owner, &release[3]);
    f = make_tc_frame(0x123, 0x456, release, sizeof(release));
    lcc_traction_handle_frame(&n, &f);
    assert(t.controller_owner == 0);

    /* RELEASE by someone else does NOT touch the slot. */
    t.controller_owner = owner;
    lcc_node_id_to_buf(0xDEADBEEF1234ULL, &release[3]);
    f = make_tc_frame(0x123, 0x456, release, sizeof(release));
    lcc_traction_handle_frame(&n, &f);
    assert(t.controller_owner == owner);

    printf("  PASS: controller assign/query/release\n");
}

/* ---- CONSIST_CFG (ATTACH / QUERY / DETACH) ----------------------------- */

static void test_consist_attach_query_detach(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0x456);

    uint64_t slave = 0x060100000007ULL;
    uint8_t attach[9] = { LCC_TRAC_CONSIST_CFG, LCC_CNSTREQ_ATTACH_NODE,
                          LCC_CNSTFLAGS_LINKF0 | LCC_CNSTFLAGS_LINKFN };
    lcc_node_id_to_buf(slave, &attach[3]);
    lcc_frame_t f = make_tc_frame(0x123, 0x456, attach, sizeof(attach));
    lcc_traction_handle_frame(&n, &f);

    assert(last_reply.payload[0] == LCC_TRAC_CONSIST_CFG);
    assert(last_reply.payload[1] == LCC_CNSTREQ_ATTACH_NODE);
    assert(last_reply.payload[8] == 0 && last_reply.payload[9] == 0);  /* OK */
    assert(t.consist[0].node_id == slave);

    /* QUERY short form: count in payload[2]. */
    reset_all();
    init_node_with_train(&n, &t, 42, false, 0x456);
    t.consist[0].node_id = slave;
    t.consist[0].flags   = LCC_CNSTFLAGS_LINKFN;
    uint8_t query[2] = { LCC_TRAC_CONSIST_CFG, LCC_CNSTREQ_QUERY_NODES };
    f = make_tc_frame(0x123, 0x456, query, sizeof(query));
    lcc_traction_handle_frame(&n, &f);
    assert(last_reply.payload_len == 3);
    assert(last_reply.payload[2] == 1);

    /* QUERY long form: index in payload[2] returns the entry + flags. */
    uint8_t query_idx[3] = { LCC_TRAC_CONSIST_CFG, LCC_CNSTREQ_QUERY_NODES, 0 };
    f = make_tc_frame(0x123, 0x456, query_idx, sizeof(query_idx));
    lcc_traction_handle_frame(&n, &f);
    assert(last_reply.payload_len == 11);
    assert(last_reply.payload[3] == 0);
    assert(last_reply.payload[4] == LCC_CNSTFLAGS_LINKFN);
    assert(lcc_node_id_from_buf(&last_reply.payload[5]) == slave);

    /* DETACH clears the slot. */
    reset_all();
    init_node_with_train(&n, &t, 42, false, 0x456);
    t.consist[0].node_id = slave;
    uint8_t detach[9] = { LCC_TRAC_CONSIST_CFG, LCC_CNSTREQ_DETACH_NODE, 0 };
    lcc_node_id_to_buf(slave, &detach[3]);
    f = make_tc_frame(0x123, 0x456, detach, sizeof(detach));
    lcc_traction_handle_frame(&n, &f);
    assert(t.consist[0].node_id == 0);

    printf("  PASS: consist attach/query/detach\n");
}

/* ---- multi-frame addressed reassembly ---------------------------------- */

/* ASSIGN with controller NID fits in 9 bytes total — just over the 6-byte
 * addressed-payload cap, so the sender splits it across two CAN frames.
 * Verify the reassembly slot in lcc_train_state reaches the dispatcher. */
static void test_addressed_reassembly(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0x456);

    uint64_t owner = 0x0A0B0C0D0E0FULL;
    uint8_t full[9] = { LCC_TRAC_CONTROLLER_CFG, LCC_CTRLREQ_ASSIGN, 0x00 };
    lcc_node_id_to_buf(owner, &full[3]);

    /* Frame 1: not-last. */
    lcc_frame_t f1;
    f1.id = lcc_can_id(LCC_MTI_TRACTION_CONTROL_COMMAND, 0x123);
    lcc_frame_build_addressed_hdr(f1.data, 0x456, LCC_NOT_LAST_FRAME);
    memcpy(&f1.data[2], &full[0], 6);
    f1.dlc = 8;
    lcc_traction_handle_frame(&n, &f1);
    assert(t.controller_owner == 0);  /* no dispatch yet */

    /* Frame 2: not-first + last. */
    lcc_frame_t f2;
    f2.id = lcc_can_id(LCC_MTI_TRACTION_CONTROL_COMMAND, 0x123);
    lcc_frame_build_addressed_hdr(f2.data, 0x456, LCC_NOT_FIRST_FRAME);
    memcpy(&f2.data[2], &full[6], 3);
    f2.dlc = 5;
    lcc_traction_handle_frame(&n, &f2);

    assert(t.controller_owner == owner);

    printf("  PASS: multi-frame addressed reassembly\n");
}

/* ---- hooks detach is safe ---------------------------------------------- */

static void test_hooks_null_detach(void) {
    reset_all();
    lcc_traction_set_hooks(NULL);

    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 3, false, 0x222);

    uint8_t cmd[3] = { LCC_TRAC_SET_SPEED_DIR, 0x00, 0x00 };
    lcc_frame_t f = make_tc_frame(0x123, 0x222, cmd, sizeof(cmd));
    lcc_traction_handle_frame(&n, &f);

    /* No crash; hook counters are zero. */
    assert(last_throttle.calls == 0);

    printf("  PASS: null hooks detach\n");
}

int main(void) {
    printf("test_lcc_traction:\n");
    test_set_speed_forward();
    test_set_speed_reverse();
    test_set_speed_stop();
    test_set_speed_nan_estop();
    test_set_fn_basic();
    test_set_fn_high_range();
    test_emergency_stop();
    test_query_speeds_reply();
    test_query_function_reply();
    test_controller_assign_query_release();
    test_consist_attach_query_detach();
    test_addressed_reassembly();
    test_hooks_null_detach();
    printf("All LCC traction tests passed.\n");
    return 0;
}
