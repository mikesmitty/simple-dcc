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
    (void)t; track_power_calls++; last_track_power = on;
}

/* dcc engine mocks — configure_mem writes don't touch DCC, but the
 * emergency-event path does. */
void dcc_set_throttle(dcc_engine_t *d, uint16_t a, uint8_t s, bool f) {
    (void)d; (void)a; (void)s; (void)f;
}
void dcc_set_function(dcc_engine_t *d, uint16_t a, uint16_t fn, bool on) {
    (void)d; (void)a; (void)fn; (void)on;
}
void dcc_emergency_stop(dcc_engine_t *d, uint16_t a) { (void)d; (void)a; }
static int dcc_estop_all_calls;
void dcc_emergency_stop_all(dcc_engine_t *d) { (void)d; dcc_estop_all_calls++; }
static int dcc_ensure_loco_calls;
void dcc_ensure_loco(dcc_engine_t *d, uint16_t a) {
    (void)d; (void)a; dcc_ensure_loco_calls++;
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
 * train nodes; tests inspect g_cs_node directly via source include. */
void lcc_task_init(void) {}
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

bool lcc_events_register(lcc_node_t *node, uint64_t event_id, uint8_t flags,
                         lcc_event_on_report_fn on_report) {
    (void)node; (void)event_id; (void)flags; (void)on_report;
    return true;
}

/* ---- CDI blob stub ------------------------------------------------------ */

/* Real _cdi_data is generated from cdi.xml. The test only asserts that
 * g_cdi_size == strlen(_cdi_data); any non-empty NUL-terminated buffer
 * stands in. */
const uint8_t _cdi_data[] = "<cdi></cdi>";

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

    nv_initialized = false;
    memset(nv_data, 0, sizeof(nv_data));
    cs_config_dirty = false;
    memset(cs_config_mem, 0, sizeof(cs_config_mem));

    /* Zero out the train pool between tests so the allocator gets a
     * clean slate on each run. */
    memset(g_train_used, 0, sizeof(g_train_used));
}

/* ---- tests ------------------------------------------------------------- */

/* Board ID is 0x1122334455667788. get_unique_node_id() uses the last 4
 * bytes: 0x55667788 → CS node ID 0x060155667788. Train NID base is the
 * well-known 0x060100000000 (not derived from board ID). */
static void test_node_id_generation(void) {
    reset_all();

    lcc_interface_load_config();
    lcc_interface_init(&mock_dcc_engine, &mock_track_main, NULL);

    uint64_t cs_id = g_cs_node.id;
    assert((cs_id & 0xFFFF00000000ULL) == 0x060100000000ULL);
    assert((cs_id & 0x0000FFFFFFFFULL) == 0x0000055667788ULL);

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
     * alone and does NOT re-dirty. */
    nv_initialized = true;
    memset(nv_data, 0, sizeof(nv_data));
    nv_data[CONFIG_OFFSET_RAILCOM] = 0;      /* non-default on purpose */
    nv_data[CONFIG_OFFSET_MAIN_LIMIT]     = (MAX_CURRENT_MAIN_MA >> 8) & 0xFF;
    nv_data[CONFIG_OFFSET_MAIN_LIMIT + 1] = MAX_CURRENT_MAIN_MA & 0xFF;
    nv_data[CONFIG_OFFSET_PINS_MAIN]      = PIN_SIGNAL_A;
    cs_config_dirty = false;

    lcc_interface_load_config();
    assert(cs_config_dirty == false);
    assert(cs_config_mem[CONFIG_OFFSET_RAILCOM] == 0);

    printf("  PASS: config migration\n");
}

/* JMRI writes to memory space 0xFD land on cs_config_mem and trigger
 * targeted side effects: RailCom toggles wavegen mode, track current
 * limits push down to the motor drivers. */
static void test_dynamic_updates(void) {
    reset_all();

    lcc_interface_load_config();
    lcc_interface_init(&mock_dcc_engine, &mock_track_main, NULL);

    /* RailCom off → wavegen reinit to NO_CUTOUT. */
    wavegen_reinit_count = 0;
    uint8_t off = 0;
    config_write(&g_cs_node, CONFIG_OFFSET_RAILCOM, 1, &off);
    assert(wavegen_reinit_count == 1);
    assert(last_wavegen_mode == WAVEGEN_NO_CUTOUT);

    /* RailCom on → wavegen reinit to NORMAL. */
    uint8_t on = 1;
    config_write(&g_cs_node, CONFIG_OFFSET_RAILCOM, 1, &on);
    assert(wavegen_reinit_count == 2);
    assert(last_wavegen_mode == WAVEGEN_NORMAL);

    /* Main limit: 2-byte write updates motor_a. */
    motor_a_limit_calls = 0;
    uint8_t limit[2] = { (uint8_t)((1500 >> 8) & 0xFF), (uint8_t)(1500 & 0xFF) };
    config_write(&g_cs_node, CONFIG_OFFSET_MAIN_LIMIT, 2, limit);
    assert(motor_a_limit_calls == 1);
    assert(motor_a_last_limit == 1500);

    /* Prog limit updates motor_b. */
    motor_b_limit_calls = 0;
    uint8_t plimit[2] = { 0x00, 0xFA };  /* 250 mA */
    config_write(&g_cs_node, CONFIG_OFFSET_PROG_LIMIT, 2, plimit);
    assert(motor_b_limit_calls == 1);
    assert(motor_b_last_limit == 250);

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

int main(void) {
    printf("test_lcc_interface:\n");
    test_node_id_generation();
    test_config_migration();
    test_dynamic_updates();
    test_allocator_and_events();
    test_emergency_events();
    printf("All LCC interface tests passed.\n");
    return 0;
}
