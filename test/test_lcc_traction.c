#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "lcc/lcc_traction.h"
#include "lcc/lcc_node.h"
#include "lcc/lcc_frame.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_float16.h"
#include "lcc/lcc_memconfig.h"

/* ---- DCC hook capture ---------------------------------------------------- */

typedef struct {
    uint16_t addr;
    bool     is_long;
    uint8_t  step;
    bool     forward;
    int      calls;
} throttle_rec_t;

typedef struct {
    uint16_t addr;
    bool     is_long;
    uint16_t fn;
    bool     on;
    int      calls;
} function_rec_t;

typedef struct {
    uint16_t addr;
    bool     is_long;
    int      calls;
} estop_rec_t;

typedef struct {
    uint16_t addr;
    bool     is_long;
    uint16_t cv;
    uint8_t  value;
    int      calls;
} pom_rec_t;

static throttle_rec_t last_throttle;
static function_rec_t last_function;
static estop_rec_t    last_estop;
static pom_rec_t      last_pom;

static void mock_set_throttle(uint16_t addr, bool is_long, uint8_t step, bool forward) {
    last_throttle.addr = addr;
    last_throttle.is_long = is_long;
    last_throttle.step = step;
    last_throttle.forward = forward;
    last_throttle.calls++;
}
static void mock_set_function(uint16_t addr, bool is_long, uint16_t fn, bool on) {
    last_function.addr = addr;
    last_function.is_long = is_long;
    last_function.fn = fn;
    last_function.on = on;
    last_function.calls++;
}
static void mock_emergency_stop(uint16_t addr, bool is_long) {
    last_estop.addr = addr;
    last_estop.is_long = is_long;
    last_estop.calls++;
}
static void mock_pom_write_byte(uint16_t addr, bool is_long,
                                uint16_t cv, uint8_t value) {
    last_pom.addr = addr;
    last_pom.is_long = is_long;
    last_pom.cv = cv;
    last_pom.value = value;
    last_pom.calls++;
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

/* ---- lcc_datagram mocks ------------------------------------------------- */
/* Pulled in because lcc_memconfig.c (linked for the CV-space tests) emits
 * datagram replies. Capture last reply so the CV-space tests can assert. */

typedef struct {
    uint16_t dst_alias;
    uint8_t  data[72];
    uint16_t len;
    int      calls;
} dg_rec_t;

static dg_rec_t last_datagram;
static dg_rec_t last_datagram_ok;

void lcc_datagram_send(const lcc_node_t *node, uint16_t dst_alias,
                       const uint8_t *data, uint8_t len) {
    (void)node;
    last_datagram.dst_alias = dst_alias;
    last_datagram.len = len < sizeof(last_datagram.data)
                           ? len : (uint16_t)sizeof(last_datagram.data);
    memcpy(last_datagram.data, data, last_datagram.len);
    last_datagram.calls++;
}
void lcc_datagram_send_ok(const lcc_node_t *node, uint16_t dst_alias) {
    (void)node;
    last_datagram_ok.dst_alias = dst_alias;
    last_datagram_ok.calls++;
}

/* ---- helpers ------------------------------------------------------------ */

static void reset_all(void) {
    memset(&last_throttle, 0, sizeof(last_throttle));
    memset(&last_function, 0, sizeof(last_function));
    memset(&last_estop, 0, sizeof(last_estop));
    memset(&last_pom, 0, sizeof(last_pom));
    memset(&last_datagram, 0, sizeof(last_datagram));
    memset(&last_datagram_ok, 0, sizeof(last_datagram_ok));
    memset(&last_reply, 0, sizeof(last_reply));
    memset(g_mock_registry, 0, sizeof(g_mock_registry));
    g_mock_count = 0;

    lcc_traction_hooks_t hooks = {
        .set_throttle   = mock_set_throttle,
        .set_function   = mock_set_function,
        .emergency_stop = mock_emergency_stop,
        .pom_write_byte = mock_pom_write_byte,
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

/* ---- CV space (0xF8) ---------------------------------------------------- */

/* Memory-config write-command datagram payload:
 *   [0x20][0x00][addr:4 be][space:1][data...]   (space-field form)
 * We always use the explicit-space form for 0xF8. */
static uint8_t mc_write(uint8_t *buf, uint32_t addr, const uint8_t *data,
                        uint8_t data_len) {
    buf[0] = 0x20;                /* memconfig protocol */
    buf[1] = 0x00;                /* WRITE, explicit space follows */
    buf[2] = (uint8_t)(addr >> 24);
    buf[3] = (uint8_t)(addr >> 16);
    buf[4] = (uint8_t)(addr >>  8);
    buf[5] = (uint8_t)(addr      );
    buf[6] = 0xF8;                /* traction CV space */
    memcpy(&buf[7], data, data_len);
    return (uint8_t)(7 + data_len);
}

/* Write a CV-space address on `n` and check the Write Reply: OK when `err`
 * is 0, else Failed carrying `err`. */
static void assert_write_reply(lcc_node_t *n, uint32_t addr,
                               const uint8_t *data, uint8_t data_len,
                               uint16_t err) {
    uint8_t pkt[24];
    uint8_t len = mc_write(pkt, addr, data, data_len);
    last_datagram.calls = 0;
    lcc_memconfig_handle(n, 0x123, pkt, len);

    assert(last_datagram.calls == 1);
    assert(last_datagram.data[1] == (err ? 0x18 : 0x10));
    assert(last_datagram.data[2] == (uint8_t)(addr >> 24));
    assert(last_datagram.data[5] == (uint8_t)addr);
    assert(last_datagram.data[6] == 0xF8);
    if (err) {
        assert(last_datagram.len == 9);
        assert(last_datagram.data[7] == (uint8_t)(err >> 8));
        assert(last_datagram.data[8] == (uint8_t)err);
    } else {
        assert(last_datagram.len == 7);
    }
}

static void test_cv_space_write_triggers_pom(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0xABC);
    lcc_traction_register_cv_space(&n);

    /* Step 1: latch CV index 29 (big-endian 4 bytes at 0x7F000000). */
    uint8_t pkt[16];
    uint8_t idx_be[4] = { 0, 0, 0, 29 };
    uint8_t len = mc_write(pkt, 0x7F000000u, idx_be, 4);
    lcc_memconfig_handle(&n, 0x123, pkt, len);
    assert(last_pom.calls == 0);         /* index latch doesn't POM */
    assert(t.pom_cv_index == 29);

    /* Step 2: write CV value 0x55 at 0x7F000004 → POM write. */
    uint8_t val = 0x55;
    len = mc_write(pkt, 0x7F000004u, &val, 1);
    lcc_memconfig_handle(&n, 0x123, pkt, len);
    assert(last_pom.calls == 1);
    assert(last_pom.addr == 42);
    assert(last_pom.is_long == false);
    assert(last_pom.cv == 29);
    assert(last_pom.value == 0x55);

    printf("  PASS: CV space write triggers POM\n");
}

static void test_cv_space_long_address(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 3000, true, 0xABC);
    lcc_traction_register_cv_space(&n);

    uint8_t pkt[16];
    uint8_t idx_be[4] = { 0, 0, 0, 1 };       /* CV 1 */
    uint8_t len = mc_write(pkt, 0x7F000000u, idx_be, 4);
    lcc_memconfig_handle(&n, 0x123, pkt, len);

    uint8_t val = 0x99;
    len = mc_write(pkt, 0x7F000004u, &val, 1);
    lcc_memconfig_handle(&n, 0x123, pkt, len);

    assert(last_pom.calls == 1);
    assert(last_pom.addr == 3000);
    assert(last_pom.is_long == true);
    assert(last_pom.cv == 1);
    assert(last_pom.value == 0x99);

    printf("  PASS: CV space honours long-address flag\n");
}

static void test_cv_space_value_before_index_fails(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0xABC);
    lcc_traction_register_cv_space(&n);

    /* CV_VALUE without a CV latched in CV_INDEX first: nothing to write. */
    uint8_t val = 0x55;
    assert_write_reply(&n, 0x7F000004u, &val, 1, LCC_ERR_INVALID_ARGUMENTS);
    assert(last_pom.calls == 0);

    /* Nor with a CV that POM can't address. */
    t.pom_cv_index = 1025;
    assert_write_reply(&n, 0x7F000004u, &val, 1, LCC_ERR_INVALID_ARGUMENTS);
    assert(last_pom.calls == 0);
    printf("  PASS: CV space value write needs a valid latched CV\n");
}

static void test_cv_space_per_node_isolation(void) {
    reset_all();
    /* Two trains with distinct CV index state — writes to one must not
     * leak to the other's POM. */
    lcc_node_t n1; lcc_train_state_t t1;
    init_node_with_train(&n1, &t1, 10, false, 0x111);
    lcc_traction_register_cv_space(&n1);

    lcc_node_t n2; lcc_train_state_t t2;
    init_node_with_train(&n2, &t2, 20, false, 0x222);
    lcc_traction_register_cv_space(&n2);

    uint8_t pkt[16];
    uint8_t idx_be[4] = { 0, 0, 0, 7 };       /* n1 gets CV 7 */
    uint8_t len = mc_write(pkt, 0x7F000000u, idx_be, 4);
    lcc_memconfig_handle(&n1, 0x123, pkt, len);

    idx_be[3] = 99;                            /* n2 gets CV 99 */
    len = mc_write(pkt, 0x7F000000u, idx_be, 4);
    lcc_memconfig_handle(&n2, 0x123, pkt, len);

    assert(t1.pom_cv_index == 7);
    assert(t2.pom_cv_index == 99);

    /* POM on n1 must use CV 7, not 99. */
    uint8_t val = 0xAA;
    len = mc_write(pkt, 0x7F000004u, &val, 1);
    lcc_memconfig_handle(&n1, 0x123, pkt, len);
    assert(last_pom.calls == 1);
    assert(last_pom.addr == 10);
    assert(last_pom.cv == 7);

    printf("  PASS: CV space is per-node isolated\n");
}

/* Memory-config read datagram with explicit-space form:
 *   [0x20][0x40][addr:4 be][space:1][count:1] */
static uint8_t mc_read(uint8_t *buf, uint32_t addr, uint8_t count) {
    buf[0] = 0x20;
    buf[1] = 0x40;
    buf[2] = (uint8_t)(addr >> 24);
    buf[3] = (uint8_t)(addr >> 16);
    buf[4] = (uint8_t)(addr >>  8);
    buf[5] = (uint8_t)(addr      );
    buf[6] = 0xF8;
    buf[7] = count;
    return 8;
}

static void test_cv_space_direct_write_poms(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0xABC);
    lcc_traction_register_cv_space(&n);

    /* JMRI ops-mode programmer: write CV 29 at address 28, no index latch. */
    uint8_t pkt[16];
    uint8_t val = 0x06;
    uint8_t len = mc_write(pkt, 28, &val, 1);
    lcc_memconfig_handle(&n, 0x123, pkt, len);

    assert(last_pom.calls == 1);
    assert(last_pom.addr == 42);
    assert(last_pom.is_long == false);
    assert(last_pom.cv == 29);
    assert(last_pom.value == 0x06);
    assert(t.pom_cv_index == 0);         /* direct path leaves the latch alone */
    const uint8_t ok[] = { 0x20, 0x10, 0, 0, 0, 28, 0xF8 };
    assert(last_datagram.len == sizeof(ok));
    assert(memcmp(last_datagram.data, ok, sizeof(ok)) == 0);

    printf("  PASS: CV space direct-address write triggers POM\n");
}

static void test_cv_space_direct_write_long_address(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 3000, true, 0xABC);
    lcc_traction_register_cv_space(&n);

    uint8_t pkt[16];
    uint8_t val = 0x99;
    uint8_t len = mc_write(pkt, 0, &val, 1);    /* CV 1 */
    lcc_memconfig_handle(&n, 0x123, pkt, len);

    assert(last_pom.calls == 1);
    assert(last_pom.addr == 3000);
    assert(last_pom.is_long == true);
    assert(last_pom.cv == 1);

    len = mc_write(pkt, 1023, &val, 1);         /* CV 1024, last direct CV */
    lcc_memconfig_handle(&n, 0x123, pkt, len);
    assert(last_pom.calls == 2);
    assert(last_pom.cv == 1024);

    printf("  PASS: CV space direct-address write honors long address\n");
}

static void test_cv_space_unwritable_addresses_fail(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0xABC);
    lcc_traction_register_cv_space(&n);

    uint8_t val = 0x55;
    assert_write_reply(&n, 1024, &val, 1, LCC_ERR_OUT_OF_BOUNDS); /* CV 1025 */
    assert_write_reply(&n, 0x7EFFFFFFu, &val, 1, LCC_ERR_OUT_OF_BOUNDS);
    assert_write_reply(&n, 0x7F000006u, &val, 1, LCC_ERR_WRITE_READ_ONLY);
    assert(last_pom.calls == 0);

    /* VERIFY_VALUE is still accepted (and dropped) until Phase 3. */
    assert_write_reply(&n, 0x7F000005u, &val, 1, 0);

    /* A run that crosses CV 1024 writes CV 1024, then fails. */
    uint8_t two[2] = { 0x11, 0x22 };
    assert_write_reply(&n, 1023, two, 2, LCC_ERR_OUT_OF_BOUNDS);
    assert(last_pom.calls == 1 && last_pom.cv == 1024 && last_pom.value == 0x11);
    printf("  PASS: CV space writes it can't apply fail with an error\n");
}

static void test_cv_space_multi_byte_direct_write(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0xABC);
    lcc_traction_register_cv_space(&n);

    /* Each byte is the next CV, as on the programming track. */
    uint8_t vals[3] = { 0x03, 0x04, 0x05 };
    assert_write_reply(&n, 0, vals, 3, 0);       /* CVs 1..3 */
    assert(last_pom.calls == 3);
    assert(last_pom.cv == 3 && last_pom.value == 0x05);
    printf("  PASS: CV space multi-byte direct write POMs each CV\n");
}

static void assert_cv_read_fails(lcc_node_t *n, uint32_t addr, uint16_t err) {
    uint8_t pkt[16];
    uint8_t len = mc_read(pkt, addr, 1);
    last_datagram.calls = 0;
    lcc_memconfig_handle(n, 0x123, pkt, len);

    assert(last_datagram.calls == 1);
    assert(last_datagram.len == 9);
    assert(last_datagram.data[1] == 0x58);      /* READ_REPLY | FAILED */
    assert(last_datagram.data[2] == (uint8_t)(addr >> 24));
    assert(last_datagram.data[5] == (uint8_t)addr);
    assert(last_datagram.data[6] == 0xF8);
    assert(last_datagram.data[7] == (uint8_t)(err >> 8));
    assert(last_datagram.data[8] == (uint8_t)err);
}

static void test_cv_space_reads_fail_without_railcom(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0xABC);
    lcc_traction_register_cv_space(&n);

    /* Reading a CV on the main needs RailCom; until then it fails with
     * "not implemented", which JMRI shows as such. */
    assert_cv_read_fails(&n, 28, LCC_ERR_UNIMPLEMENTED_CMD);        /* CV 29 */
    assert_cv_read_fails(&n, 1023, LCC_ERR_UNIMPLEMENTED_CMD);      /* CV 1024 */
    assert_cv_read_fails(&n, 0x7F000004u, LCC_ERR_UNIMPLEMENTED_CMD);
    assert_cv_read_fails(&n, 0x7F000006u, LCC_ERR_UNIMPLEMENTED_CMD);
    /* Nothing lives between the direct CVs and the indirect registers. */
    assert_cv_read_fails(&n, 1024, LCC_ERR_OUT_OF_BOUNDS);
    assert_cv_read_fails(&n, 0x7EFFFFFFu, LCC_ERR_OUT_OF_BOUNDS);
    assert(last_pom.calls == 0);

    /* The CV index register still reads back. */
    t.pom_cv_index = 0x0102;
    uint8_t pkt[16];
    uint8_t len = mc_read(pkt, 0x7F000000u, 4);
    last_datagram.calls = 0;
    lcc_memconfig_handle(&n, 0x123, pkt, len);
    const uint8_t want[] = { 0x20, 0x50, 0x7F, 0, 0, 0, 0xF8, 0, 0, 1, 2 };
    assert(last_datagram.calls == 1);
    assert(last_datagram.len == sizeof(want));
    assert(memcmp(last_datagram.data, want, sizeof(want)) == 0);

    printf("  PASS: CV space reads fail with an error until RailCom\n");
}

static void test_cv_space_get_space_info(void) {
    reset_all();
    lcc_node_t n; lcc_train_state_t t;
    init_node_with_train(&n, &t, 42, false, 0xABC);
    lcc_traction_register_cv_space(&n);

    /* GET_SPACE_INFO 0x84 for space 0xF8 → node's handler wins, reply
     * reports highest-address = 0x7F000006 and read-only = 0. */
    uint8_t pkt[3] = { 0x20, 0x84, 0xF8 };
    lcc_memconfig_handle(&n, 0x123, pkt, sizeof(pkt));

    assert(last_datagram.calls == 1);
    assert(last_datagram.len == 8);
    assert(last_datagram.data[0] == 0x20);
    assert(last_datagram.data[1] == 0x87);        /* SPACE_INFO_REPLY */
    assert(last_datagram.data[2] == 0xF8);
    /* highest = 0x7F000006 */
    assert(last_datagram.data[3] == 0x7F);
    assert(last_datagram.data[4] == 0x00);
    assert(last_datagram.data[5] == 0x00);
    assert(last_datagram.data[6] == 0x06);
    assert(last_datagram.data[7] == 0x00);        /* flags: writable */

    printf("  PASS: CV space GET_SPACE_INFO reply\n");
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
    test_cv_space_write_triggers_pom();
    test_cv_space_long_address();
    test_cv_space_value_before_index_fails();
    test_cv_space_per_node_isolation();
    test_cv_space_get_space_info();
    test_cv_space_direct_write_poms();
    test_cv_space_direct_write_long_address();
    test_cv_space_unwritable_addresses_fail();
    test_cv_space_multi_byte_direct_write();
    test_cv_space_reads_fail_without_railcom();
    printf("All LCC traction tests passed.\n");
    return 0;
}
