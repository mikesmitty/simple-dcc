#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---- FreeRTOS + Pico host mocks ---------------------------------------- */

#include "FreeRTOS.h"
#include "pico/unique_id.h"

void pico_get_unique_board_id(pico_unique_board_id_t *id_out) {
    /* Predictable ID — tests assert on the last 4 bytes. */
    for (int i = 0; i < 8; i++) id_out->id[i] = (uint8_t)((i + 1) * 0x11);
}

/* xTimerChangePeriod isn't in the headless FreeRTOS mock; stub it. */
static inline int xTimerChangePeriod(TimerHandle_t x, TickType_t p, TickType_t t) {
    (void)x; (void)p; (void)t; return 1;
}

/* hardware/watchdog.h mock — reboot is never actually exercised. */
static int g_watchdog_calls;
static inline void watchdog_reboot(uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c; g_watchdog_calls++;
}
static inline void vTaskDelay_host(TickType_t t) { (void)t; }
#define vTaskDelay vTaskDelay_host

/* ---- nv_storage mock (in-RAM flash shadow) ----------------------------- */

static bool    nv_initialized;
static uint8_t nv_data[512];

bool nv_storage_init(void *mem, size_t size) {
    if (!nv_initialized) return false;
    memcpy(mem, nv_data, size);
    return true;
}
bool nv_storage_write(const void *mem, size_t size) {
    memcpy(nv_data, mem, size);
    nv_initialized = true;
    return true;
}

/* ---- wavegen / motor / track hooks ------------------------------------- */

#include "wavegen/wavegen.h"
#include "motor/motor.h"
#include "track/track.h"
#include "dcc/dcc.h"

wavegen_t wavegen = { .initialized = true };
motor_t   motor_a;
motor_t   motor_b;

static int            wavegen_reinit_count;
static wavegen_mode_t last_wavegen_mode;

bool wavegen_reinit(wavegen_t *wg, wavegen_mode_t mode,
                    uint signal_pin, uint signal_pin_count, uint brake_pin) {
    (void)signal_pin; (void)signal_pin_count; (void)brake_pin;
    wavegen_reinit_count++;
    last_wavegen_mode = mode;
    wg->mode = mode;
    return true;
}

static int      motor_a_limit_calls;
static int      motor_b_limit_calls;
static uint16_t motor_a_last_limit;
static uint16_t motor_b_last_limit;

void motor_set_current_limit_ma(motor_t *m, uint16_t ma) {
    if (m == &motor_a) { motor_a_limit_calls++; motor_a_last_limit = ma; }
    if (m == &motor_b) { motor_b_limit_calls++; motor_b_last_limit = ma; }
}

static int  track_power_calls;
static bool last_track_power;
void track_set_power(track_t *t, bool on) {
    t->power_on = on; track_power_calls++; last_track_power = on;
}

/* dcc engine mocks — configure_mem writes don't touch DCC, but the
 * emergency-event path does. */
void dcc_set_throttle(dcc_engine_t *d, uint16_t a, bool is_long,
                       uint8_t s, bool f) {
    (void)d; (void)a; (void)is_long; (void)s; (void)f;
}
void dcc_set_function(dcc_engine_t *d, uint16_t a, bool is_long,
                       uint16_t fn, bool on) {
    (void)d; (void)a; (void)is_long; (void)fn; (void)on;
}
void dcc_emergency_stop(dcc_engine_t *d, uint16_t a, bool is_long) {
    (void)d; (void)a; (void)is_long;
}
static int dcc_estop_all_calls;
void dcc_emergency_stop_all(dcc_engine_t *d) { (void)d; dcc_estop_all_calls++; }
static int dcc_ensure_loco_calls;
void dcc_ensure_loco(dcc_engine_t *d, uint16_t a, bool is_long) {
    (void)d; (void)a; (void)is_long; dcc_ensure_loco_calls++;
}
void dcc_pom_write_byte(dcc_engine_t *d, uint16_t a, bool is_long,
                        uint16_t cv, uint8_t v) {
    (void)d; (void)a; (void)is_long; (void)cv; (void)v;
}

/* ---- LCC module mocks -------------------------------------------------- */

#include "lcc/lcc_node.h"
#include "lcc/lcc_frame.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_memconfig.h"
#include "lcc/lcc_events.h"
#include "lcc/lcc_traction.h"
#include "lcc/lcc_train_search.h"
#include "lcc/lcc_task.h"
#include "lcc/lcc_snip.h"

/* Node registry capture — lcc_interface.c registers the CS node + any
 * train nodes; tests inspect g_cs_node directly via source include.
 * The real lcc_events.c is linked; its table is reset here, as the real
 * lcc_task_init does. */
void lcc_task_init(void) { lcc_events_reset(); }
void lcc_task_run(void)  {}
void lcc_node_init(lcc_node_t *node, uint64_t id, lcc_node_role_t role) {
    memset(node, 0, sizeof(*node));
    node->id = id;
    node->role = role;
    node->alias_state = LCC_A_IDLE;
}
bool lcc_node_register(lcc_node_t *node) { (void)node; return true; }

/* Capture hooks set by lcc_interface.c so tests can invoke them. */
static const lcc_traction_hooks_t     *captured_traction_hooks;
static lcc_train_alloc_fn              captured_allocator;
static const lcc_memspace_handler_t   *captured_spaces;
static uint8_t                         captured_space_count;
static lcc_memconfig_reboot_fn         captured_reboot_hook;
static lcc_memconfig_factory_reset_fn  captured_factory_reset_hook;

void lcc_traction_set_hooks(const lcc_traction_hooks_t *hooks) {
    captured_traction_hooks = hooks;
}
void lcc_traction_register_cv_space(lcc_node_t *node) { (void)node; }
void lcc_train_state_init(lcc_train_state_t *state, uint16_t addr, bool is_long) {
    memset(state, 0, sizeof(*state));
    state->dcc_address = addr;
    state->is_long_address = is_long;
}

void lcc_train_search_set_allocator(lcc_train_alloc_fn alloc) {
    captured_allocator = alloc;
}
void lcc_memconfig_register_spaces(const lcc_memspace_handler_t *spaces,
                                   uint8_t count) {
    captured_spaces = spaces;
    captured_space_count = count;
}
void lcc_memconfig_set_hooks(lcc_memconfig_reboot_fn reboot,
                             lcc_memconfig_factory_reset_fn factory_reset) {
    captured_reboot_hook = reboot;
    captured_factory_reset_hook = factory_reset;
}

static const lcc_node_t             *captured_node_spaces_node;
static const lcc_memspace_handler_t *captured_node_spaces;
static uint8_t                       captured_node_space_count;
void lcc_memconfig_register_node_spaces(lcc_node_t *node,
                                        const lcc_memspace_handler_t *spaces,
                                        uint8_t count) {
    captured_node_spaces_node = node;
    captured_node_spaces = spaces;
    captured_node_space_count = count;
}

static int      complete_calls;
static uint16_t complete_error;
static uint8_t  complete_data[4];
static uint8_t  complete_len;
void lcc_memconfig_complete(uint16_t error, const uint8_t *data, uint8_t len) {
    complete_calls++;
    complete_error = error;
    complete_len = len;
    if (data && len) memcpy(complete_data, data, len);
}

/* Global sends from lcc_events.c: the Identified replies. */
typedef struct {
    uint16_t mti;
    uint64_t event_id;
} sent_event_t;
static sent_event_t sent_events[16];
static int          sent_event_count;

void lcc_node_send_global(const lcc_node_t *node, uint16_t mti,
                          const uint8_t *data, uint8_t dlc) {
    (void)node;
    if (sent_event_count < 16) {
        sent_events[sent_event_count].mti = mti;
        sent_events[sent_event_count].event_id =
            dlc == 8 ? lcc_event_from_buf(data) : 0;
        sent_event_count++;
    }
}

/* ---- MCP2515 LCC transport stub ----------------------------------------- */

/* lcc_interface_maybe_start_mcp2515() is called from init if the CDI
 * "enabled" flag is set. Host tests never enable it, so this stub is
 * only needed to satisfy the linker. */
#include "mcp2515/mcp2515_lcc.h"
bool mcp2515_lcc_start(const mcp2515_config_t *cfg) { (void)cfg; return true; }

/* ---- CDI blob stub ------------------------------------------------------ */

/* Real _cdi_data is generated from cdi.xml. The test only asserts that
 * g_cdi_size == strlen(_cdi_data); any non-empty NUL-terminated buffer
 * stands in. */
const uint8_t _cdi_data[] = "<cdi></cdi>";

/* ---- servicemode stub ------------------------------------------------- */

/* The real driver is PIO + ADC. Capture submissions so the tests can
 * check the 0xF8 → CV mapping and drive the completion callback. */
#include "servicemode/servicemode.h"

static int                 svc_submit_calls;
static svc_op_t            svc_last_op;
static uint16_t            svc_last_cv;
static uint8_t             svc_last_value;
static servicemode_done_fn svc_last_done;
static void               *svc_last_ctx;
static svc_result_t        svc_submit_result;

svc_result_t servicemode_submit(svc_op_t op, uint16_t cv, uint8_t value,
                                servicemode_done_fn done, void *ctx) {
    svc_submit_calls++;
    svc_last_op = op;
    svc_last_cv = cv;
    svc_last_value = value;
    svc_last_done = done;
    svc_last_ctx = ctx;
    return svc_submit_result;
}

/* ---- lcc_interface.c under test ---------------------------------------- */

#include "../src/protocol/lcc_interface.c"

/* ---- helpers ----------------------------------------------------------- */

static track_t mock_track_main = { .motor = &motor_a };
static dcc_engine_t mock_dcc_engine;

static void reset_all(void) {
    wavegen_reinit_count = 0;
    motor_a_limit_calls = motor_b_limit_calls = 0;
    motor_a_last_limit = motor_b_last_limit = 0;
    track_power_calls = 0;
    dcc_estop_all_calls = 0;
    dcc_ensure_loco_calls = 0;
    g_watchdog_calls = 0;
    captured_traction_hooks = NULL;
    captured_allocator = NULL;
    captured_spaces = NULL;
    captured_space_count = 0;
    captured_reboot_hook = NULL;
    captured_factory_reset_hook = NULL;
    captured_node_spaces_node = NULL;
    captured_node_spaces = NULL;
    captured_node_space_count = 0;
    sent_event_count = 0;
    mock_track_main.power_on = false;
    complete_calls = 0;
    complete_error = 0;
    complete_len = 0;
    memset(complete_data, 0, sizeof(complete_data));
    svc_submit_calls = 0;
    svc_last_done = NULL;
    svc_submit_result = SVC_OK;

    nv_initialized = false;
    memset(nv_data, 0, sizeof(nv_data));
    cs_config_dirty = false;
    memset(cs_config_mem, 0, sizeof(cs_config_mem));

    /* Zero out the train pool between tests so the allocator gets a
     * clean slate on each run. */
    memset(g_train_used, 0, sizeof(g_train_used));
}

/* Hand lcc_events.c a frame from another node and collect the replies. */
static void event_frame(uint16_t mti, uint64_t event_id) {
    lcc_frame_t f = { .id = lcc_can_id(mti, 0x123) };
    if (event_id) {
        lcc_event_to_buf(event_id, f.data);
        f.dlc = 8;
    }
    sent_event_count = 0;
    lcc_events_handle_frame(&g_cs_node, &f);
}

/* MTI of the one reply sent for `event_id`, or 0 if there was none. */
static uint16_t reply_mti(uint64_t event_id) {
    uint16_t mti = 0;
    for (int i = 0; i < sent_event_count; i++) {
        if (sent_events[i].event_id != event_id) continue;
        assert(mti == 0);
        mti = sent_events[i].mti;
    }
    return mti;
}

/* ---- tests ------------------------------------------------------------- */

/* Board ID is 0x1122334455667788. get_unique_node_id() uses the last 5
 * bytes in the locally allocated range: CS node ID 0x0A4455667788, kept
 * out of the 06.01 train node range. Train NID base is the well-known
 * 0x060100000000 (not derived from board ID). */
static void test_node_id_generation(void) {
    reset_all();

    lcc_interface_load_config();
    lcc_interface_init(&mock_dcc_engine, &mock_track_main, NULL);

    uint64_t cs_id = g_cs_node.id;
    assert(cs_id == 0x0A4455667788ULL);
    assert((cs_id & 0xFFFF00000000ULL) != LCC_TRAIN_NODE_ID_BASE);

    uint64_t train_base = lcc_interface_get_train_node_id_base();
    assert(train_base == LCC_TRAIN_NODE_ID_BASE);

    printf("  PASS: node id generation\n");
}

/* Empty flash and old/incomplete flash both should fall back to the
 * compiled defaults + mark the config dirty so it writes back. */
static void test_config_migration(void) {
    reset_all();

    /* Empty flash: load_config populates defaults. */
    lcc_interface_load_config();
    assert(cs_config_dirty == true);
    assert(cs_config_mem[CONFIG_OFFSET_RAILCOM] == 1);
    assert(cs_config_mem[CONFIG_OFFSET_AUTO_CLAIM] == 1);
    assert(lcc_interface_main_limit_ma() == MAX_CURRENT_MAIN_MA);
    assert(cs_config_mem[CONFIG_OFFSET_PINS_MAIN] == PIN_SIGNAL_A);
    assert(cs_config_mem[CONFIG_OFFSET_DISPLAY_SDA]  == DISPLAY_DEFAULT_SDA);
    assert(cs_config_mem[CONFIG_OFFSET_DISPLAY_SCL]  == DISPLAY_DEFAULT_SCL);
    assert(cs_config_mem[CONFIG_OFFSET_DISPLAY_ADDR] == DISPLAY_DEFAULT_I2C_ADDR);
    assert(cs_config_mem[CONFIG_OFFSET_DISPLAY_EN]   == 1);
    assert(cs_config_mem[CONFIG_OFFSET_PINS_MAIN + 3] == PIN_FAULT_A);
    assert(cs_config_mem[CONFIG_OFFSET_PINS_PROG + 3] == PIN_FAULT_B);
    assert(cs_config_mem[CONFIG_OFFSET_VERSION] == CONFIG_VERSION);

    /* "Old" flash: auto-claim present but no limits/pins — migration
     * should overwrite with defaults. */
    nv_initialized = true;
    memset(nv_data, 0, sizeof(nv_data));
    nv_data[CONFIG_OFFSET_AUTO_CLAIM] = 1;
    cs_config_dirty = false;

    lcc_interface_load_config();
    assert(cs_config_dirty == true);
    assert(cs_config_mem[CONFIG_OFFSET_RAILCOM] == 1);
    assert(lcc_interface_main_limit_ma() == MAX_CURRENT_MAIN_MA);
    assert(cs_config_mem[CONFIG_OFFSET_PINS_MAIN] == PIN_SIGNAL_A);

    /* "Recent" flash: values already populated — migration leaves them
     * alone and does NOT re-dirty. Includes MCP2515 SCK and the current
     * version so neither migration fires; at the current version a fault
     * pin of 4 is a deliberate choice and stays. */
    nv_initialized = true;
    memset(nv_data, 0, sizeof(nv_data));
    nv_data[CONFIG_OFFSET_RAILCOM] = 0;      /* non-default on purpose */
    nv_data[CONFIG_OFFSET_MAIN_LIMIT]     = (MAX_CURRENT_MAIN_MA >> 8) & 0xFF;
    nv_data[CONFIG_OFFSET_MAIN_LIMIT + 1] = MAX_CURRENT_MAIN_MA & 0xFF;
    nv_data[CONFIG_OFFSET_PINS_MAIN]      = PIN_SIGNAL_A;
    nv_data[CONFIG_OFFSET_PINS_MAIN + 3]  = 4;
    nv_data[CONFIG_OFFSET_DISPLAY_ADDR]   = DISPLAY_DEFAULT_I2C_ADDR;
    nv_data[CONFIG_OFFSET_MCP_SCK]        = MCP2515_DEFAULT_SCK;
    nv_data[CONFIG_OFFSET_VERSION]        = CONFIG_VERSION;
    cs_config_dirty = false;

    lcc_interface_load_config();
    assert(cs_config_dirty == false);
    assert(cs_config_mem[CONFIG_OFFSET_RAILCOM] == 0);
    assert(cs_config_mem[CONFIG_OFFSET_PINS_MAIN + 3] == 4);

    /* Version 0 flash with the old GPIO 4/5 fault defaults: the version 1
     * migration moves them to the shield's A4/A5 pins, keeps other user
     * values, and dirties the config so the new version is written back. */
    nv_initialized = true;
    memset(nv_data, 0, sizeof(nv_data));
    nv_data[CONFIG_OFFSET_RAILCOM]        = 0;
    nv_data[CONFIG_OFFSET_MAIN_LIMIT]     = (MAX_CURRENT_MAIN_MA >> 8) & 0xFF;
    nv_data[CONFIG_OFFSET_MAIN_LIMIT + 1] = MAX_CURRENT_MAIN_MA & 0xFF;
    nv_data[CONFIG_OFFSET_PINS_MAIN]      = PIN_SIGNAL_A;
    nv_data[CONFIG_OFFSET_PINS_MAIN + 3]  = 4;
    nv_data[CONFIG_OFFSET_PINS_PROG + 3]  = 5;
    nv_data[CONFIG_OFFSET_DISPLAY_ADDR]   = DISPLAY_DEFAULT_I2C_ADDR;
    nv_data[CONFIG_OFFSET_MCP_SCK]        = MCP2515_DEFAULT_SCK;
    cs_config_dirty = false;

    lcc_interface_load_config();
    assert(cs_config_dirty == true);
    assert(cs_config_mem[CONFIG_OFFSET_RAILCOM] == 0);  /* user value preserved */
    assert(cs_config_mem[CONFIG_OFFSET_PINS_MAIN + 3] == PIN_FAULT_A);
    assert(cs_config_mem[CONFIG_OFFSET_PINS_PROG + 3] == PIN_FAULT_B);
    assert(cs_config_mem[CONFIG_OFFSET_VERSION] == CONFIG_VERSION);

    /* Version 0 flash with custom fault pins: only the version moves. */
    nv_data[CONFIG_OFFSET_PINS_MAIN + 3]  = 7;
    nv_data[CONFIG_OFFSET_PINS_PROG + 3]  = 6;
    cs_config_dirty = false;

    lcc_interface_load_config();
    assert(cs_config_dirty == true);
    assert(cs_config_mem[CONFIG_OFFSET_PINS_MAIN + 3] == 7);
    assert(cs_config_mem[CONFIG_OFFSET_PINS_PROG + 3] == 6);
    assert(cs_config_mem[CONFIG_OFFSET_VERSION] == CONFIG_VERSION);

    /* Pre-MCP flash (v0.4.0 and earlier): MCP_SCK == 0 triggers the
     * partial MCP-only migration — other user values preserved, but
     * dirty-bit set so the fresh MCP defaults get written back. */
    nv_initialized = true;
    memset(nv_data, 0, sizeof(nv_data));
    nv_data[CONFIG_OFFSET_RAILCOM]        = 0;
    nv_data[CONFIG_OFFSET_MAIN_LIMIT]     = (MAX_CURRENT_MAIN_MA >> 8) & 0xFF;
    nv_data[CONFIG_OFFSET_MAIN_LIMIT + 1] = MAX_CURRENT_MAIN_MA & 0xFF;
    nv_data[CONFIG_OFFSET_PINS_MAIN]      = PIN_SIGNAL_A;
    nv_data[CONFIG_OFFSET_DISPLAY_ADDR]   = DISPLAY_DEFAULT_I2C_ADDR;
    /* MCP_SCK left at 0 to trigger migration */
    cs_config_dirty = false;

    lcc_interface_load_config();
    assert(cs_config_dirty == true);
    assert(cs_config_mem[CONFIG_OFFSET_RAILCOM] == 0);  /* user value preserved */
    assert(cs_config_mem[CONFIG_OFFSET_MCP_SCK] == MCP2515_DEFAULT_SCK);
    assert(cs_config_mem[CONFIG_OFFSET_MCP_EN]  == MCP2515_DEFAULT_ENABLED);

    printf("  PASS: config migration\n");
}

/* JMRI writes to memory space 0xFD land on cs_config_mem and trigger
 * targeted side effects: RailCom toggles wavegen mode, track current
 * limits push down to the motor drivers. */
static void test_dynamic_updates(void) {
    reset_all();

    lcc_interface_load_config();
    lcc_interface_init(&mock_dcc_engine, &mock_track_main, NULL);

    uint16_t err = 0;

    /* RailCom off → wavegen reinit to NO_CUTOUT. */
    wavegen_reinit_count = 0;
    uint8_t off = 0;
    config_write(&g_cs_node, CONFIG_OFFSET_RAILCOM, 1, &off, &err);
    assert(wavegen_reinit_count == 1);
    assert(last_wavegen_mode == WAVEGEN_NO_CUTOUT);

    /* RailCom on → wavegen reinit to NORMAL. */
    uint8_t on = 1;
    config_write(&g_cs_node, CONFIG_OFFSET_RAILCOM, 1, &on, &err);
    assert(wavegen_reinit_count == 2);
    assert(last_wavegen_mode == WAVEGEN_NORMAL);

    /* Main limit: 2-byte write updates motor_a. */
    motor_a_limit_calls = 0;
    uint8_t limit[2] = { (uint8_t)((1500 >> 8) & 0xFF), (uint8_t)(1500 & 0xFF) };
    config_write(&g_cs_node, CONFIG_OFFSET_MAIN_LIMIT, 2, limit, &err);
    assert(motor_a_limit_calls == 1);
    assert(motor_a_last_limit == 1500);

    /* Prog limit updates motor_b. */
    motor_b_limit_calls = 0;
    uint8_t plimit[2] = { 0x00, 0xFA };  /* 250 mA */
    config_write(&g_cs_node, CONFIG_OFFSET_PROG_LIMIT, 2, plimit, &err);
    assert(motor_b_limit_calls == 1);
    assert(motor_b_last_limit == 250);
    assert(err == 0);

    /* Pin getters read straight from cs_config_mem. */
    uint8_t sig, pwr, brk, flt, adc;
    lcc_interface_get_pins_main(&sig, &pwr, &brk, &flt, &adc);
    assert(sig == PIN_SIGNAL_A);
    assert(pwr == PIN_POWER_A);

    printf("  PASS: dynamic updates\n");
}

/* The VNI-probe + Train Search paths both end up calling the allocator
 * captured via lcc_train_search_set_allocator. Exercise it directly:
 * short + long addresses, idempotency, and emergency-event side
 * effects via the captured memconfig hooks. */
static void test_allocator_and_events(void) {
    reset_all();

    lcc_interface_load_config();
    lcc_interface_init(&mock_dcc_engine, &mock_track_main, NULL);

    assert(captured_allocator != NULL);
    assert(captured_reboot_hook != NULL);
    assert(captured_factory_reset_hook != NULL);
    assert(captured_traction_hooks != NULL);

    /* Allocate a short-address train. */
    lcc_node_t *n = captured_allocator(42, LCC_TRAIN_SEARCH_FLAG_DCC, 0);
    assert(n != NULL);
    assert(n->id == (LCC_TRAIN_NODE_ID_BASE | 42));
    assert(n->role == LCC_ROLE_TRAIN);
    assert(n->train != NULL);
    assert(n->train->dcc_address == 42);
    assert(n->train->is_long_address == false);
    assert(dcc_ensure_loco_calls == 1);

    /* Idempotent: a second allocate for the same address returns the
     * same slot and does not burn another ensure_loco call. */
    lcc_node_t *n2 = captured_allocator(42, LCC_TRAIN_SEARCH_FLAG_DCC, 0);
    assert(n2 == n);
    assert(dcc_ensure_loco_calls == 1);

    /* Long-address path honours the LONG_ADDR flag. */
    lcc_node_t *n_long = captured_allocator(3,
        LCC_TRAIN_SEARCH_FLAG_DCC | LCC_TRAIN_SEARCH_FLAG_LONG_ADDR, 0);
    assert(n_long != NULL);
    assert(n_long->train->is_long_address == true);

    /* And an address > 127 is promoted to long even without the flag. */
    lcc_node_t *n_big = captured_allocator(1234, LCC_TRAIN_SEARCH_FLAG_DCC, 0);
    assert(n_big != NULL);
    assert(n_big->train->is_long_address == true);

    printf("  PASS: allocator short/long/idempotent\n");
}

/* EVENT_REPORT for the well-known emergency events: power off cuts
 * track power, estop-all calls dcc_emergency_stop_all. Captured
 * handle_emergency_event is invoked via the lcc_events_register path
 * but since we mocked lcc_events_register, we invoke the registered
 * handler through the internal static handle_emergency_event symbol
 * directly (file-include exposes it). */
static void test_emergency_events(void) {
    reset_all();
    lcc_interface_load_config();
    lcc_interface_init(&mock_dcc_engine, &mock_track_main, NULL);

    track_power_calls = 0;
    dcc_estop_all_calls = 0;

    handle_emergency_event(&g_cs_node, LCC_EVENT_EMERGENCY_OFF);
    assert(track_power_calls == 1);
    assert(last_track_power == false);

    handle_emergency_event(&g_cs_node, LCC_EVENT_CLEAR_EMERG_OFF);
    assert(track_power_calls == 2);
    assert(last_track_power == true);

    handle_emergency_event(&g_cs_node, LCC_EVENT_EMERGENCY_STOP);
    assert(dcc_estop_all_calls == 1);

    /* Unknown event: no-op. */
    handle_emergency_event(&g_cs_node, 0x0123456789ABCDEFULL);
    assert(track_power_calls == 2);
    assert(dcc_estop_all_calls == 1);

    printf("  PASS: emergency event handler\n");
}

/* Programming track: the CS node publishes 0xF8 with a deferred handler
 * and produces IS_PROGRAMMING_TRACK; the handler maps address → CV,
 * submits to the Service Mode driver, and turns results into JMRI's
 * OlcbProgrammer error codes. */
static void test_programming_track(void) {
    reset_all();
    lcc_interface_load_config();
    lcc_interface_init(&mock_dcc_engine, &mock_track_main, NULL);

    assert(captured_node_spaces_node == &g_cs_node);
    assert(captured_node_space_count == 1);
    const lcc_memspace_handler_t *h = &captured_node_spaces[0];
    assert(h->space == LCC_SPACE_DCC_CV);
    assert(h->size == 1024);
    assert(h->defer != NULL);

    event_frame(LCC_MTI_PRODUCER_IDENTIFY, LCC_EVENT_IS_PROGRAMMING_TRACK);
    assert(reply_mti(LCC_EVENT_IS_PROGRAMMING_TRACK)
           == LCC_MTI_PRODUCER_IDENTIFIED_UNKNOWN);

    /* Read CV 29 (address 28). */
    lcc_memconfig_deferred_t rd = { .is_write = false, .space = 0xF8,
                                    .addr = 28, .count = 1, .data = NULL };
    assert(h->defer(&g_cs_node, &rd) == 0);
    assert(svc_submit_calls == 1);
    assert(svc_last_op == SVC_OP_READ);
    assert(svc_last_cv == 29);
    svc_last_done(SVC_OK, 0x06, svc_last_ctx);
    assert(complete_calls == 1);
    assert(complete_error == 0);
    assert(complete_len == 1 && complete_data[0] == 0x06);

    svc_last_done(SVC_NO_LOCO, 0, svc_last_ctx);
    assert(complete_error == LCC_ERR_DCC_NO_LOCO);
    assert(complete_len == 0);

    /* Write CV 1 = 3. */
    uint8_t three = 3;
    lcc_memconfig_deferred_t wr = { .is_write = true, .space = 0xF8,
                                    .addr = 0, .count = 1, .data = &three };
    assert(h->defer(&g_cs_node, &wr) == 0);
    assert(svc_last_op == SVC_OP_WRITE);
    assert(svc_last_cv == 1);
    assert(svc_last_value == 3);
    svc_last_done(SVC_OK, 3, svc_last_ctx);
    assert(complete_error == 0 && complete_len == 0);
    svc_last_done(SVC_NO_ACK, 3, svc_last_ctx);
    assert(complete_error == LCC_ERR_DCC_FAILED_VERIFY);
    svc_last_done(SVC_SHORT, 3, svc_last_ctx);
    assert(complete_error == LCC_ERR_DCC_PGM_SHORT);

    /* Multi-byte writes are refused, not half-applied. */
    uint8_t two[2] = { 1, 2 };
    lcc_memconfig_deferred_t wr2 = { .is_write = true, .space = 0xF8,
                                     .addr = 0, .count = 2, .data = two };
    int before = svc_submit_calls;
    assert(h->defer(&g_cs_node, &wr2) == LCC_ERR_INVALID_ARGUMENTS);
    assert(svc_submit_calls == before);

    /* Driver refusals come straight back as error codes. */
    svc_submit_result = SVC_BUSY;
    assert(h->defer(&g_cs_node, &rd) == LCC_ERR_TEMPORARY);
    svc_submit_result = SVC_UNAVAILABLE;
    assert(h->defer(&g_cs_node, &rd) == LCC_ERR_DCC_PGM_DISABLED);

    printf("  PASS: programming track wiring\n");
}

/* Every way the CS identifies the emergency events gives the same
 * states for the given main power. */
static void check_emergency_states(uint16_t off_mti, uint16_t clear_mti) {
    static const uint64_t events[4] = {
        LCC_EVENT_EMERGENCY_OFF, LCC_EVENT_CLEAR_EMERG_OFF,
        LCC_EVENT_EMERGENCY_STOP, LCC_EVENT_CLEAR_EMERG_STOP,
    };
    const uint16_t want[4] = {
        off_mti, clear_mti,
        LCC_MTI_CONSUMER_IDENTIFIED_UNKNOWN, LCC_MTI_CONSUMER_IDENTIFIED_UNKNOWN,
    };

    for (int i = 0; i < 4; i++) {
        event_frame(LCC_MTI_CONSUMER_IDENTIFY, events[i]);
        assert(sent_event_count == 1);
        assert(reply_mti(events[i]) == want[i]);
    }

    /* Identify Events, global and addressed, and the announcement after
     * Initialization Complete. */
    for (int pass = 0; pass < 3; pass++) {
        if (pass == 0) {
            event_frame(LCC_MTI_EVENTS_IDENTIFY_GLOBAL, 0);
        } else if (pass == 1) {
            event_frame(LCC_MTI_EVENTS_IDENTIFY_ADDRESSED, 0);
        } else {
            sent_event_count = 0;
            lcc_events_announce(&g_cs_node);
        }
        for (int i = 0; i < 4; i++)
            assert(reply_mti(events[i]) == want[i]);
        assert(reply_mti(LCC_EVENT_IS_PROGRAMMING_TRACK)
               == LCC_MTI_PRODUCER_IDENTIFIED_UNKNOWN);
    }
}

/* Emergency Off is Valid while main power is off and Clear Emergency Off
 * while it is on; the e-stop pair stays Unknown. */
static void test_power_event_state(void) {
    reset_all();
    lcc_interface_load_config();
    lcc_interface_init(&mock_dcc_engine, &mock_track_main, NULL);

    /* Off at boot. */
    check_emergency_states(LCC_MTI_CONSUMER_IDENTIFIED_VALID,
                           LCC_MTI_CONSUMER_IDENTIFIED_INVALID);

    event_frame(LCC_MTI_EVENT_REPORT, LCC_EVENT_CLEAR_EMERG_OFF);
    assert(last_track_power == true);
    check_emergency_states(LCC_MTI_CONSUMER_IDENTIFIED_INVALID,
                           LCC_MTI_CONSUMER_IDENTIFIED_VALID);

    event_frame(LCC_MTI_EVENT_REPORT, LCC_EVENT_EMERGENCY_OFF);
    assert(last_track_power == false);
    check_emergency_states(LCC_MTI_CONSUMER_IDENTIFIED_VALID,
                           LCC_MTI_CONSUMER_IDENTIFIED_INVALID);

    printf("  PASS: power state in emergency event replies\n");
}

int main(void) {
    printf("test_lcc_interface:\n");
    test_node_id_generation();
    test_config_migration();
    test_dynamic_updates();
    test_allocator_and_events();
    test_emergency_events();
    test_programming_track();
    test_power_event_state();
    printf("All LCC interface tests passed.\n");
    return 0;
}
