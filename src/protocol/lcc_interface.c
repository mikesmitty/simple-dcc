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

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "pico/unique_id.h"
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
// Exposed on the GridConnect bus via lcc_node_register. The SNIP user
// fields stay NULL until M4 wires ACDI (config space 0xFB) into the
// reply path — the handler emits empty strings for NULL fields.
static lcc_node_t g_cs_node;
static const lcc_snip_t g_cs_snip = {
    .mfg_name   = "simple-dcc",
    .model_name = "DCC Command Station",
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

    /* Build and register the CS node. M3 advertises the minimum protocol
     * set that JMRI needs to show the node: SNIP + PIP + identification.
     * Memory-config + event-exchange advertisements come in M4/M5. */
    lcc_node_init(&g_cs_node, get_unique_node_id(), LCC_ROLE_CS);
    g_cs_node.snip     = &g_cs_snip;
    g_cs_node.pip_bits = LCC_PIP_SIMPLE_PROTOCOL
                       | LCC_PIP_IDENTIFICATION
                       | LCC_PIP_SIMPLE_NODE_INFO;
    lcc_node_register(&g_cs_node);
}

void task_protocol(void *params) {
    (void)params;
    lcc_task_run();
}
