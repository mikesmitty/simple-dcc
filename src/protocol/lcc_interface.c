#include "protocol/lcc_interface.h"
#include "serial/serial.h"
#include "util/nv_storage.h"
#include "board_config.h"
#include "wavegen/wavegen.h"
#include "motor/motor.h"
#include "lcc/lcc_task.h"
#include "lcc/lcc_node.h"
#include "lcc/lcc_snip.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_memconfig.h"
#include "lcc/lcc_events.h"
#include "lcc/lcc_traction.h"
#include "lcc/lcc_train_search.h"
#include "cdi_data.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "pico/unique_id.h"
#include "hardware/watchdog.h"
#include "util/dbg.h"
#include <string.h>

/* Release-please patches the literal below on each release. CI builds
 * override it via a compile-time define (derived from `git describe`),
 * so development binaries carry the git-sha suffix while tagged
 * releases report the tag verbatim. */
#ifndef LCC_SW_VERSION
// x-release-please-start-version
#define LCC_SW_VERSION "0.4.0"
// x-release-please-end
#endif

dcc_engine_t *g_dcc_engine;
static track_t *g_track_main;
// Flag accessed from multiple tasks and the timer task. SMP-safe via __atomic_*.
static bool    cs_config_dirty = false;
static TimerHandle_t flash_flush_timer;

// Train node ID base derived from hardware (0x060100000000 proxy prefix)
static uint64_t g_train_node_id_base;

// --- Command-station LCC node ---
// The CS is the single OpenLCB node exposed by M4; PIP advertises the
// full protocol set so JMRI treats it as a command station. SNIP user
// fields come from cs_config_mem via the ACDI 0xFB projection.
static lcc_node_t g_cs_node;
static const lcc_snip_t g_cs_snip = {
    .mfg_name   = "simple-dcc",
    .model_name = "DCC Command Station",
    .hw_version = "RP2350",
    .sw_version = LCC_SW_VERSION,
    .user_name  = NULL,
    .user_desc  = NULL,
};

// --- Train node pool (M7 dynamic allocation) ---
// DCC-proxy train nodes are spawned on demand by the Train Search allocator.
// The CS occupies one slot in the lcc_node registry, so the pool caps at
// LCC_MAX_NODES - 1. Storage is static: no malloc anywhere in the stack.
#define LCC_TRAIN_POOL_SIZE      (LCC_MAX_NODES - 1)
static lcc_node_t        g_train_nodes [LCC_TRAIN_POOL_SIZE];
static lcc_train_state_t g_train_states[LCC_TRAIN_POOL_SIZE];
static bool              g_train_used  [LCC_TRAIN_POOL_SIZE];

static const lcc_snip_t  g_train_snip = {
    .mfg_name   = "simple-dcc",
    .model_name = "DCC Train Proxy",
    .hw_version = "RP2350",
    .sw_version = LCC_SW_VERSION,
    .user_name  = NULL,
    .user_desc  = NULL,
};

// --- Configuration memory (RAM-backed with Flash persistence, space 0xFD) ---

#define CONFIG_OFFSET_AUTO_CLAIM  127   // after ACDI user fields (63 + 64)
#define CONFIG_OFFSET_RAILCOM     128
#define CONFIG_OFFSET_MAIN_LIMIT  129   // 2 bytes
#define CONFIG_OFFSET_PROG_LIMIT  131   // 2 bytes
#define CONFIG_OFFSET_PINS_MAIN   133   // 5 bytes (Signal, Power, Brake, Fault, ADC)
#define CONFIG_OFFSET_PINS_PROG   138   // 5 bytes (Signal, Power, Brake, Fault, ADC)
#define CONFIG_MEM_SIZE           0x0200

static uint8_t cs_config_mem[CONFIG_MEM_SIZE] __attribute__((aligned(256)));

static void config_mem_init_defaults(void) {
    memset(cs_config_mem, 0, CONFIG_MEM_SIZE);
    cs_config_mem[CONFIG_OFFSET_AUTO_CLAIM] = 1;  // default: enabled
    cs_config_mem[CONFIG_OFFSET_RAILCOM] = 1;     // default: enabled

    cs_config_mem[CONFIG_OFFSET_MAIN_LIMIT] = (MAX_CURRENT_MAIN_MA >> 8) & 0xFF;
    cs_config_mem[CONFIG_OFFSET_MAIN_LIMIT+1] = MAX_CURRENT_MAIN_MA & 0xFF;
    cs_config_mem[CONFIG_OFFSET_PROG_LIMIT] = (MAX_CURRENT_PROG_MA >> 8) & 0xFF;
    cs_config_mem[CONFIG_OFFSET_PROG_LIMIT+1] = MAX_CURRENT_PROG_MA & 0xFF;

    cs_config_mem[CONFIG_OFFSET_PINS_MAIN]   = PIN_SIGNAL_A;
    cs_config_mem[CONFIG_OFFSET_PINS_MAIN+1] = PIN_POWER_A;
    cs_config_mem[CONFIG_OFFSET_PINS_MAIN+2] = PIN_BRAKE_A;
    cs_config_mem[CONFIG_OFFSET_PINS_MAIN+3] = PIN_FAULT_A;
    cs_config_mem[CONFIG_OFFSET_PINS_MAIN+4] = ADC_CHANNEL_A;

    cs_config_mem[CONFIG_OFFSET_PINS_PROG]   = PIN_SIGNAL_B;
    cs_config_mem[CONFIG_OFFSET_PINS_PROG+1] = PIN_POWER_B;
    cs_config_mem[CONFIG_OFFSET_PINS_PROG+2] = PIN_BRAKE_B;
    cs_config_mem[CONFIG_OFFSET_PINS_PROG+3] = PIN_FAULT_B;
    cs_config_mem[CONFIG_OFFSET_PINS_PROG+4] = ADC_CHANNEL_B;
}

static void mark_config_dirty(void) {
    __atomic_store_n(&cs_config_dirty, true, __ATOMIC_SEQ_CST);
    if (flash_flush_timer)
        xTimerStart(flash_flush_timer, 0);
}

static void flash_flush_timer_callback(TimerHandle_t timer) {
    (void)timer;
    if (__atomic_load_n(&cs_config_dirty, __ATOMIC_SEQ_CST)) {
        DBG("[NV] flushing config to flash\n");
        if (nv_storage_write(cs_config_mem, CONFIG_MEM_SIZE)) {
            __atomic_store_n(&cs_config_dirty, false, __ATOMIC_SEQ_CST);
        } else {
            DBG("[NV] ERROR: flash flush failed\n");
        }
    }
}

// --- Memory-space backing stores --------------------------------------

/* CDI (0xFF). The generated _cdi_data is NUL-terminated XML; the highest
 * valid address is therefore one byte short of the NUL. strlen() gives
 * that length at runtime — sizeof() cannot be used on the extern. */
static uint32_t g_cdi_size;

static uint16_t cdi_read(lcc_node_t *node, uint32_t addr, uint16_t count,
                         uint8_t *out) {
    (void)node;
    if (addr >= g_cdi_size) return 0;
    if ((uint32_t)addr + count > g_cdi_size)
        count = (uint16_t)(g_cdi_size - addr);
    memcpy(out, _cdi_data + addr, count);
    return count;
}

/* CONFIG (0xFD). Direct view of cs_config_mem. Writes mark dirty and
 * kick the flash flush timer; UPDATE_COMPLETE forces an immediate flush
 * via the same mechanism. */
static uint16_t config_read(lcc_node_t *node, uint32_t addr, uint16_t count,
                            uint8_t *out) {
    (void)node;
    if (addr >= CONFIG_MEM_SIZE) return 0;
    if ((uint32_t)addr + count > CONFIG_MEM_SIZE)
        count = (uint16_t)(CONFIG_MEM_SIZE - addr);
    memcpy(out, cs_config_mem + addr, count);
    return count;
}

/* wavegen / motor globals live in main.c; declared extern here so
 * config writes can apply dynamic changes (RailCom toggle, current
 * limits) without waiting for a reboot. Host tests stub these. */
extern wavegen_t wavegen;
extern motor_t   motor_a;
extern motor_t   motor_b;

static bool config_range_touches(uint32_t addr, uint16_t count,
                                 uint32_t field, uint32_t field_len) {
    return addr < field + field_len && addr + count > field;
}

static uint16_t config_write(lcc_node_t *node, uint32_t addr, uint16_t count,
                             const uint8_t *in) {
    (void)node;
    if (addr >= CONFIG_MEM_SIZE) return 0;
    if ((uint32_t)addr + count > CONFIG_MEM_SIZE)
        count = (uint16_t)(CONFIG_MEM_SIZE - addr);
    memcpy(cs_config_mem + addr, in, count);
    mark_config_dirty();

    /* Apply side effects for the fields with live-update semantics — the
     * DCC-side handles re-check config on each operation, so only the
     * hardware-touching fields need push updates. */
    if (config_range_touches(addr, count, CONFIG_OFFSET_RAILCOM, 1)) {
        bool enabled = cs_config_mem[CONFIG_OFFSET_RAILCOM] != 0;
        uint8_t sig, pwr, brk, flt, adc;
        lcc_interface_get_pins_main(&sig, &pwr, &brk, &flt, &adc);
        wavegen_reinit(&wavegen,
                       enabled ? WAVEGEN_NORMAL : WAVEGEN_NO_CUTOUT,
                       sig, 2, brk);
    }
    if (config_range_touches(addr, count, CONFIG_OFFSET_MAIN_LIMIT, 2)) {
        motor_set_current_limit_ma(&motor_a, lcc_interface_main_limit_ma());
    }
    if (config_range_touches(addr, count, CONFIG_OFFSET_PROG_LIMIT, 2)) {
        motor_set_current_limit_ma(&motor_b, lcc_interface_prog_limit_ma());
    }
    return count;
}

static void config_update_complete(lcc_node_t *node) {
    (void)node;
    /* Force an early flush so JMRI "Save" round-trips within a few
     * seconds instead of the normal 2s debounce. */
    if (__atomic_load_n(&cs_config_dirty, __ATOMIC_SEQ_CST)) {
        xTimerChangePeriod(flash_flush_timer, pdMS_TO_TICKS(100), 0);
    }
}

/* ACDI user (0xFB) — 128 bytes total:
 *   byte 0    : version = 2 (fixed)
 *   bytes 1..63 : user name    (projected from cs_config_mem[0..62])
 *   bytes 64..127 : user desc  (projected from cs_config_mem[63..126])
 * Writes land in cs_config_mem at the projected offsets and dirty the
 * backing store the same as a direct 0xFD write. */
#define ACDI_USER_SIZE     128
#define ACDI_USER_VERSION  2

static uint16_t acdi_user_read(lcc_node_t *node, uint32_t addr, uint16_t count,
                               uint8_t *out) {
    (void)node;
    if (addr >= ACDI_USER_SIZE) return 0;
    if ((uint32_t)addr + count > ACDI_USER_SIZE)
        count = (uint16_t)(ACDI_USER_SIZE - addr);
    for (uint16_t i = 0; i < count; i++) {
        uint32_t a = addr + i;
        out[i] = (a == 0) ? ACDI_USER_VERSION : cs_config_mem[a - 1];
    }
    return count;
}

static uint16_t acdi_user_write(lcc_node_t *node, uint32_t addr, uint16_t count,
                                const uint8_t *in) {
    (void)node;
    if (addr >= ACDI_USER_SIZE) return 0;
    if ((uint32_t)addr + count > ACDI_USER_SIZE)
        count = (uint16_t)(ACDI_USER_SIZE - addr);
    bool wrote = false;
    for (uint16_t i = 0; i < count; i++) {
        uint32_t a = addr + i;
        if (a == 0) continue;  /* version byte is read-only */
        cs_config_mem[a - 1] = in[i];
        wrote = true;
    }
    if (wrote) mark_config_dirty();
    return count;
}

/* ACDI mfg (0xFC) — 125-byte static layout per ACDI S&TN v1:
 *   byte 0       : version = 1
 *   bytes 1..41  : Manufacturer name (41 bytes, NUL-padded)
 *   bytes 42..82 : Node type        (41 bytes)
 *   bytes 83..103: Hardware version (21 bytes)
 *   bytes 104..124: Software version (21 bytes) */
#define ACDI_MFG_SIZE     125
#define ACDI_MFG_VERSION  1
static uint8_t g_acdi_mfg[ACDI_MFG_SIZE];

static void acdi_mfg_init(void) {
    memset(g_acdi_mfg, 0, sizeof(g_acdi_mfg));
    g_acdi_mfg[0] = ACDI_MFG_VERSION;
    /* strncpy-style fill: truncate on overflow, pad remainder with 0. */
    size_t n;
    n = strlen(g_cs_snip.mfg_name);   if (n > 41) n = 41;
    memcpy(&g_acdi_mfg[1], g_cs_snip.mfg_name, n);
    n = strlen(g_cs_snip.model_name); if (n > 41) n = 41;
    memcpy(&g_acdi_mfg[42], g_cs_snip.model_name, n);
    n = strlen(g_cs_snip.hw_version); if (n > 21) n = 21;
    memcpy(&g_acdi_mfg[83], g_cs_snip.hw_version, n);
    n = strlen(g_cs_snip.sw_version); if (n > 21) n = 21;
    memcpy(&g_acdi_mfg[104], g_cs_snip.sw_version, n);
}

static uint16_t acdi_mfg_read(lcc_node_t *node, uint32_t addr, uint16_t count,
                              uint8_t *out) {
    (void)node;
    if (addr >= ACDI_MFG_SIZE) return 0;
    if ((uint32_t)addr + count > ACDI_MFG_SIZE)
        count = (uint16_t)(ACDI_MFG_SIZE - addr);
    memcpy(out, g_acdi_mfg + addr, count);
    return count;
}

/* Space registration table — fed to lcc_memconfig_register_spaces() once. */
static const lcc_memspace_handler_t g_spaces[] = {
    { LCC_SPACE_CDI,       0,                 true,  cdi_read,       NULL,             NULL                    },
    { LCC_SPACE_CONFIG,    CONFIG_MEM_SIZE,   false, config_read,    config_write,     config_update_complete  },
    { LCC_SPACE_ACDI_MFG,  ACDI_MFG_SIZE,     true,  acdi_mfg_read,  NULL,             NULL                    },
    { LCC_SPACE_ACDI_USER, ACDI_USER_SIZE,    false, acdi_user_read, acdi_user_write,  config_update_complete  },
};

/* CDI size is only known at runtime — patch it in after the table is
 * registered. The memconfig module holds the table by pointer, so
 * mutating it in place is safe. */
static lcc_memspace_handler_t g_spaces_rw[sizeof(g_spaces) / sizeof(g_spaces[0])];

/* Reboot + factory-reset hooks for memconfig 0xA9 / 0xAA. */
static void memcfg_reboot_hook(void) {
    /* Small delay to let the OK datagram flush over USB CDC before the
     * chip resets. flash_flush timer not flushed here — if the user
     * intended to save, UPDATE_COMPLETE should have already fired. */
    vTaskDelay(pdMS_TO_TICKS(100));
    watchdog_reboot(0, 0, 0);
}

static void memcfg_factory_reset_hook(lcc_node_t *node) {
    (void)node;
    config_mem_init_defaults();
    __atomic_store_n(&cs_config_dirty, true, __ATOMIC_SEQ_CST);
    /* Synchronous flush before reboot so the defaults survive. */
    flash_flush_timer_callback(NULL);
    vTaskDelay(pdMS_TO_TICKS(100));
    watchdog_reboot(0, 0, 0);
}

// --- Node ID from hardware ---

static uint64_t get_unique_node_id(void) {
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    // Use last 4 bytes of the 8-byte board ID as the unique part
    // Set the top two bytes to the private range prefix (0x0601)
    uint64_t id = 0x060100000000ULL;
    id |= ((uint64_t)board_id.id[4] << 24);
    id |= ((uint64_t)board_id.id[5] << 16);
    id |= ((uint64_t)board_id.id[6] << 8);
    id |= ((uint64_t)board_id.id[7]);
    return id;
}

bool lcc_interface_auto_claim_enabled(void) {
    return cs_config_mem[CONFIG_OFFSET_AUTO_CLAIM] != 0;
}

uint64_t lcc_interface_get_train_node_id_base(void) {
    return g_train_node_id_base;
}

// --- Configuration getters ---

bool lcc_interface_railcom_enabled(void) {
    return cs_config_mem[CONFIG_OFFSET_RAILCOM] != 0;
}

uint16_t lcc_interface_main_limit_ma(void) {
    return (uint16_t)((cs_config_mem[CONFIG_OFFSET_MAIN_LIMIT] << 8) | cs_config_mem[CONFIG_OFFSET_MAIN_LIMIT+1]);
}

uint16_t lcc_interface_prog_limit_ma(void) {
    return (uint16_t)((cs_config_mem[CONFIG_OFFSET_PROG_LIMIT] << 8) | cs_config_mem[CONFIG_OFFSET_PROG_LIMIT+1]);
}

void lcc_interface_get_pins_main(uint8_t *sig, uint8_t *pwr, uint8_t *brk, uint8_t *flt, uint8_t *adc) {
    if (sig) *sig = cs_config_mem[CONFIG_OFFSET_PINS_MAIN];
    if (pwr) *pwr = cs_config_mem[CONFIG_OFFSET_PINS_MAIN+1];
    if (brk) *brk = cs_config_mem[CONFIG_OFFSET_PINS_MAIN+2];
    if (flt) *flt = cs_config_mem[CONFIG_OFFSET_PINS_MAIN+3];
    if (adc) *adc = cs_config_mem[CONFIG_OFFSET_PINS_MAIN+4];
}

void lcc_interface_get_pins_prog(uint8_t *sig, uint8_t *pwr, uint8_t *brk, uint8_t *flt, uint8_t *adc) {
    if (sig) *sig = cs_config_mem[CONFIG_OFFSET_PINS_PROG];
    if (pwr) *pwr = cs_config_mem[CONFIG_OFFSET_PINS_PROG+1];
    if (brk) *brk = cs_config_mem[CONFIG_OFFSET_PINS_PROG+2];
    if (flt) *flt = cs_config_mem[CONFIG_OFFSET_PINS_PROG+3];
    if (adc) *adc = cs_config_mem[CONFIG_OFFSET_PINS_PROG+4];
}

void lcc_interface_load_config(void) {
    // Load config from flash, or init to defaults if flash is empty.
    // Runs before the scheduler starts, so plain assignment to cs_config_dirty
    // is race-free here. Post-scheduler writes use __atomic_store_n.
    if (!nv_storage_init(cs_config_mem, CONFIG_MEM_SIZE)) {
        DBG("[NV] no config in flash, using defaults\n");
        config_mem_init_defaults();
        cs_config_dirty = true;
    } else {
        DBG("[NV] config loaded from flash\n");
        // Migration check: If the main current limit or essential pins are 0,
        // this flash was likely from an older version. Populate with new defaults.
        uint16_t main_limit = (uint16_t)((cs_config_mem[CONFIG_OFFSET_MAIN_LIMIT] << 8) | cs_config_mem[CONFIG_OFFSET_MAIN_LIMIT+1]);
        uint8_t sig_pin = cs_config_mem[CONFIG_OFFSET_PINS_MAIN];
        if (main_limit == 0 || sig_pin == 0) {
            DBG("[NV] old or incomplete config detected (limit=%d, sig=%d), applying new defaults\n", main_limit, sig_pin);
            config_mem_init_defaults();
            cs_config_dirty = true;
        }
    }
}

// --- Traction hooks -----------------------------------------------------
// Thin shims that capture g_dcc_engine so the lcc_traction module stays
// free of a dependency on the DCC engine type.

static void traction_set_throttle(uint16_t addr, bool is_long,
                                   uint8_t step, bool forward) {
    if (g_dcc_engine) dcc_set_throttle(g_dcc_engine, addr, is_long, step, forward);
}
static void traction_set_function(uint16_t addr, bool is_long,
                                   uint16_t fn, bool on) {
    if (g_dcc_engine) dcc_set_function(g_dcc_engine, addr, is_long, fn, on);
}
static void traction_emergency_stop(uint16_t addr, bool is_long) {
    if (g_dcc_engine) dcc_emergency_stop(g_dcc_engine, addr, is_long);
}

static const lcc_traction_hooks_t g_traction_hooks = {
    .set_throttle   = traction_set_throttle,
    .set_function   = traction_set_function,
    .emergency_stop = traction_emergency_stop,
};

// --- Train pool allocator (M7) -----------------------------------------
// Called by the Train Search dispatcher when a throttle's ALLOCATE query
// fails to match any registered train. Pick the first free pool slot,
// initialize the train state, register the node, and stash event_id so
// PRODUCER_IDENTIFIED_VALID can fire once the alias confirms.

static lcc_node_t *train_find_by_addr(uint16_t addr) {
    for (int i = 0; i < LCC_TRAIN_POOL_SIZE; i++) {
        if (g_train_used[i] && g_train_states[i].dcc_address == addr)
            return &g_train_nodes[i];
    }
    return NULL;
}

static lcc_node_t *train_allocator(uint16_t dcc_addr, uint8_t flags,
                                   uint64_t event_id) {
    /* Idempotent on repeat ALLOCATE: JMRI sometimes re-sends the search
     * during reconnect, and we'd lose the existing owner mapping if we
     * clobbered the state with a fresh init. */
    lcc_node_t *existing = train_find_by_addr(dcc_addr);
    if (existing) {
        if (existing->train &&
            existing->alias_state != LCC_A_CONFIRMED &&
            existing->train->pending_search_event == 0) {
            existing->train->pending_search_event = event_id;
        }
        return existing;
    }

    int slot = -1;
    for (int i = 0; i < LCC_TRAIN_POOL_SIZE; i++) {
        if (!g_train_used[i]) { slot = i; break; }
    }
    if (slot < 0) return NULL;

    bool is_long = (flags & LCC_TRAIN_SEARCH_FLAG_LONG_ADDR)
                || (dcc_addr > LCC_DCC_SHORT_ADDR_MAX);
    lcc_train_state_init(&g_train_states[slot], dcc_addr, is_long);
    g_train_states[slot].pending_search_event = event_id;

    /* OpenLCB S-9.7.0.1: long addresses OR in the 0xC000 selector. */
    uint64_t node_id = g_train_node_id_base | (uint64_t)dcc_addr;
    if (is_long) node_id |= 0xC000ULL;

    lcc_node_t *node = &g_train_nodes[slot];
    lcc_node_init(node, node_id, LCC_ROLE_TRAIN);
    node->snip     = &g_train_snip;
    node->pip_bits = LCC_PIP_SIMPLE_PROTOCOL
                   | LCC_PIP_EVENT_EXCHANGE
                   | LCC_PIP_SIMPLE_NODE_INFO
                   | LCC_PIP_TRAIN_CONTROL;
    node->train    = &g_train_states[slot];

    if (!lcc_node_register(node))
        return NULL;

    g_train_used[slot] = true;
    if (g_dcc_engine)
        dcc_ensure_loco(g_dcc_engine, dcc_addr, is_long);
    return node;
}

// --- Emergency event handling -----------------------------------------

static void handle_emergency_event(lcc_node_t *node, uint64_t event_id) {
    (void)node;
    switch (event_id) {
    case LCC_EVENT_EMERGENCY_OFF:
        DBG("[LCC] EMERGENCY_OFF: cutting track power\n");
        if (g_track_main) track_set_power(g_track_main, false);
        break;
    case LCC_EVENT_CLEAR_EMERG_OFF:
        DBG("[LCC] CLEAR_EMERG_OFF: restoring track power\n");
        if (g_track_main) track_set_power(g_track_main, true);
        break;
    case LCC_EVENT_EMERGENCY_STOP:
        DBG("[LCC] EMERGENCY_STOP: all-loco estop\n");
        if (g_dcc_engine) dcc_emergency_stop_all(g_dcc_engine);
        break;
    case LCC_EVENT_CLEAR_EMERG_STOP:
        /* No protocol-level action — throttles re-send speed as needed. */
        break;
    default:
        break;
    }
}

void lcc_interface_init(dcc_engine_t *dcc, track_t *track, QueueHandle_t pqueue_input) {
    (void)pqueue_input;

    g_dcc_engine = dcc;
    g_track_main = track;

    flash_flush_timer = xTimerCreate("flash_flush", pdMS_TO_TICKS(2000),
                                     pdFALSE, NULL, flash_flush_timer_callback);

    if (__atomic_load_n(&cs_config_dirty, __ATOMIC_SEQ_CST)) {
        xTimerStart(flash_flush_timer, 0);
    }

    // Well-known OpenLCB DCC-proxy train node ID prefix: 06.01.00.00.<addr>.
    // Throttles (JMRI, WiThrottle, etc.) probe for trains using this fixed
    // prefix, so the CS must advertise train nodes in this range to be
    // discoverable by DCC address.
    g_train_node_id_base = 0x060100000000ULL;

    lcc_task_init();

    /* Build and register the CS node. The CS itself does NOT advertise
     * Train Control — train nodes do (registered separately below). */
    lcc_node_init(&g_cs_node, get_unique_node_id(), LCC_ROLE_CS);
    g_cs_node.snip     = &g_cs_snip;
    g_cs_node.pip_bits = LCC_PIP_SIMPLE_PROTOCOL
                       | LCC_PIP_DATAGRAM
                       | LCC_PIP_MEMORY_CONFIG
                       | LCC_PIP_EVENT_EXCHANGE
                       | LCC_PIP_IDENTIFICATION
                       | LCC_PIP_ABBREVIATED_CDI
                       | LCC_PIP_SIMPLE_NODE_INFO
                       | LCC_PIP_CDI;
    lcc_node_register(&g_cs_node);

    /* Traction DCC bridge + Train Search auto-allocation. Pool entries
     * start empty; JMRI opening a throttle on address N triggers the
     * allocator, which registers node 06.01.00.00.XX.XX for that DCC
     * address and defers PRODUCER_IDENTIFIED_VALID until the alias
     * state machine confirms. */
    lcc_traction_set_hooks(&g_traction_hooks);
    memset(g_train_used, 0, sizeof(g_train_used));
    lcc_train_search_set_allocator(train_allocator);

    /* Memory-config spaces — CDI size is computed from the generated
     * _cdi_data's NUL terminator (strlen gives XML byte count). */
    g_cdi_size = (uint32_t)strlen((const char *)_cdi_data);
    memcpy(g_spaces_rw, g_spaces, sizeof(g_spaces));
    g_spaces_rw[0].size = g_cdi_size;  /* entry 0 is LCC_SPACE_CDI */
    acdi_mfg_init();
    lcc_memconfig_register_spaces(g_spaces_rw,
        (uint8_t)(sizeof(g_spaces_rw) / sizeof(g_spaces_rw[0])));
    lcc_memconfig_set_hooks(memcfg_reboot_hook, memcfg_factory_reset_hook);

    /* Emergency event consumers (well-known OpenLCB events). */
    lcc_events_register(&g_cs_node, LCC_EVENT_EMERGENCY_OFF,
                        LCC_EVENT_CONSUMER, handle_emergency_event);
    lcc_events_register(&g_cs_node, LCC_EVENT_CLEAR_EMERG_OFF,
                        LCC_EVENT_CONSUMER, handle_emergency_event);
    lcc_events_register(&g_cs_node, LCC_EVENT_EMERGENCY_STOP,
                        LCC_EVENT_CONSUMER, handle_emergency_event);
    lcc_events_register(&g_cs_node, LCC_EVENT_CLEAR_EMERG_STOP,
                        LCC_EVENT_CONSUMER, handle_emergency_event);
}

void task_protocol(void *params) {
    (void)params;
    lcc_task_run();
}
