#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Mock FreeRTOS before including project headers
#include "FreeRTOS.h"

// Mock unique board ID
#include "pico/unique_id.h"
void pico_get_unique_board_id(pico_unique_board_id_t *id_out) {
    // 0x1122334455667788
    for(int i=0; i<8; i++) id_out->id[i] = (i+1) * 0x11;
}

// Project headers with mocks
#include "dcc/dcc.h"
#include "track/track.h"
#include "serial/serial.h"
#include "util/nv_storage.h"
#include "wavegen/wavegen.h"
#include "motor/motor.h"
#include "board_config.h"

// OpenLCB library types
#include "openlcb/openlcb_types.h"
#include "openlcb/openlcb_defines.h"
#include "drivers/canbus/can_types.h"
#include "openlcb/openlcb_gridconnect.h"
#include "openlcb/openlcb_config.h"
#include "drivers/canbus/can_config.h"

// Mock OpenLcb library functions
void OpenLcb_100ms_timer_tick(void) {}
void OpenLcb_initialize(const openlcb_config_t *config) {}
openlcb_node_t *OpenLcb_create_node(node_id_t id, const node_parameters_t *params) {
    static openlcb_node_t node;
    node.id = id;
    node.train_state = NULL;
    return &node;
}
uint16_t OpenLcbApplication_register_consumer_eventid(openlcb_node_t *openlcb_node, event_id_t event_id, event_status_enum event_status) { return 0; }
void OpenLcb_run(void) {}
openlcb_node_t *OpenLcbNode_find_by_node_id(node_id_t id) { return NULL; }

// Mock other deps
bool serial_write_ready(void) { return true; }
void serial_write(const uint8_t *buf, uint16_t len) {}
void OpenLcbGridConnect_from_can_msg(gridconnect_buffer_t *gc, can_msg_t *msg) {}

// Track nv_storage calls
static bool nv_initialized = false;
static uint8_t nv_data[512];
bool nv_storage_init(void *mem, size_t size) { 
    if (nv_initialized) {
        memcpy(mem, nv_data, size);
        return true;
    }
    return false;
}
bool nv_storage_write(const void *mem, size_t size) { 
    memcpy(nv_data, mem, size);
    nv_initialized = true;
    return true; 
}

void track_set_power(track_t *track, bool on) {}
void dcc_emergency_stop_all(dcc_engine_t *engine) {}
void CanConfig_initialize(const can_config_t *config) {}

// Wavegen mocks
wavegen_t wavegen = { .initialized = true };
static int wavegen_reinit_count = 0;
static wavegen_mode_t last_wavegen_mode;
bool wavegen_reinit(wavegen_t *wg, wavegen_mode_t mode, uint signal_pin, uint signal_pin_count, uint brake_pin) {
    wavegen_reinit_count++;
    last_wavegen_mode = mode;
    wg->mode = mode;
    return true;
}

// Motor mocks
motor_t motor_a = {0};
motor_t motor_b = {0};
static int motor_a_limit_update_count = 0;
static uint16_t motor_a_last_limit = 0;
void motor_set_current_limit_ma(motor_t *m, uint16_t ma) {
    if (m == &motor_a) {
        motor_a_limit_update_count++;
        motor_a_last_limit = ma;
    }
}

// Global mock objects
track_t mock_track = { .motor = &motor_a };
const node_parameters_t OpenLcbUserConfig_node_parameters = {0};

// Include the source to be tested
#include "../src/protocol/lcc_interface.c"

// Helper to mock traction callbacks
void lcc_traction_on_speed_changed(openlcb_node_t *node, uint16_t speed) {}
void lcc_traction_on_function_changed(openlcb_node_t *node, uint32_t fn, uint16_t val) {}
void lcc_traction_on_emergency_entered(openlcb_node_t *node, train_emergency_type_enum type) {}
void lcc_traction_on_emergency_exited(openlcb_node_t *node, train_emergency_type_enum type) {}
void lcc_traction_on_controller_released(openlcb_node_t *node) {}
openlcb_node_t *lcc_traction_on_search_no_match(uint16_t addr, uint8_t flags) { return NULL; }

node_id_t CanUtilities_extract_can_payload_as_node_id(can_msg_t *msg) {
    node_id_t id = 0;
    for(int i=0; i<6; i++) id = (id << 8) | msg->payload[i];
    return id;
}

uint16_t CanUtilities_convert_can_mti_to_openlcb_mti(can_msg_t *msg) {
    return (uint16_t)msg->identifier; 
}

static void test_node_id_generation(void) {
    lcc_interface_load_config();
    lcc_interface_init(NULL, &mock_track, NULL);
    
    node_id_t cs_id = g_cs_node->id;
    // board ID was 0x1122334455667788
    // get_unique_node_id uses last 4 bytes: 0x55667788
    // cs_id should be 0x060155667788
    assert((cs_id & 0xFFFF00000000ULL) == 0x060100000000ULL);
    assert((cs_id & 0x0000FFFFFFFFULL) == 0x000055667788ULL);

    node_id_t train_base = lcc_interface_get_train_node_id_base();
    assert(train_base == 0x060155660000ULL);

    printf("  PASS: node id generation\n");
}

static void test_auto_discovery_logic(void) {
    lcc_interface_load_config();
    lcc_interface_init(NULL, &mock_track, NULL);
    
    can_msg_t msg;
    msg.identifier = MTI_VERIFY_NODE_ID_GLOBAL;
    msg.payload_count = 6;
    msg.payload[0] = 0x06;
    msg.payload[1] = 0x01;
    msg.payload[2] = 0x55;
    msg.payload[3] = 0x66; // Matches our base
    msg.payload[4] = 0x00;
    msg.payload[5] = 0x03; 

    pending_train_addr = 0;
    lcc_interface_on_rx_can_msg(&msg);
    assert(pending_train_addr == 3);

    // Wrong base
    msg.payload[3] = 0x77; 
    pending_train_addr = 0;
    lcc_interface_on_rx_can_msg(&msg);
    assert(pending_train_addr == 0);

    printf("  PASS: auto discovery logic\n");
}

static void test_config_migration(void) {
    // Test migration from empty flash
    nv_initialized = false;
    memset(nv_data, 0, sizeof(nv_data));
    cs_config_dirty = false;
    
    lcc_interface_load_config();
    assert(cs_config_dirty == true);
    assert(cs_config_mem[CONFIG_OFFSET_RAILCOM] == 1);
    assert(lcc_interface_main_limit_ma() == MAX_CURRENT_MAIN_MA);
    assert(cs_config_mem[CONFIG_OFFSET_PINS_MAIN] == PIN_SIGNAL_A);
    
    // Test migration from "old" flash (limit=0)
    nv_initialized = true;
    memset(nv_data, 0, sizeof(nv_data));
    nv_data[CONFIG_OFFSET_AUTO_CLAIM] = 1; // existing data
    // limits and pins are 0
    cs_config_dirty = false;

    lcc_interface_load_config();
    assert(cs_config_dirty == true);
    assert(cs_config_mem[CONFIG_OFFSET_RAILCOM] == 1);
    assert(lcc_interface_main_limit_ma() == MAX_CURRENT_MAIN_MA);
    
    printf("  PASS: config migration\n");
}

static void test_dynamic_updates(void) {
    lcc_interface_load_config();
    lcc_interface_init(NULL, &mock_track, NULL);
    
    // Test RailCom toggle
    wavegen_reinit_count = 0;
    uint8_t val = 0; // Disable RailCom
    config_mem_write(g_cs_node, CONFIG_OFFSET_RAILCOM, 1, (configuration_memory_buffer_t*)&val);
    assert(wavegen_reinit_count == 1);
    assert(last_wavegen_mode == WAVEGEN_NO_CUTOUT);
    
    val = 1; // Enable RailCom
    config_mem_write(g_cs_node, CONFIG_OFFSET_RAILCOM, 1, (configuration_memory_buffer_t*)&val);
    assert(wavegen_reinit_count == 2);
    assert(last_wavegen_mode == WAVEGEN_NORMAL);
    
    // Test current limit update
    motor_a_limit_update_count = 0;
    uint8_t limit_val[2] = { (uint8_t)((1500 >> 8) & 0xFF), (uint8_t)(1500 & 0xFF) };
    config_mem_write(g_cs_node, CONFIG_OFFSET_MAIN_LIMIT, 2, (configuration_memory_buffer_t*)limit_val);
    assert(motor_a_limit_update_count == 1);
    assert(motor_a_last_limit == 1500);
    
    // Test pin getters
    uint8_t sig, pwr, brk, flt, adc;
    lcc_interface_get_pins_main(&sig, &pwr, &brk, &flt, &adc);
    assert(sig == PIN_SIGNAL_A);
    assert(pwr == PIN_POWER_A);
    
    printf("  PASS: dynamic updates\n");
}

int main(void) {
    printf("test_lcc_interface:\n");
    test_node_id_generation();
    test_auto_discovery_logic();
    test_config_migration();
    test_dynamic_updates();
    printf("All LCC interface tests passed.\n");
    return 0;
}
