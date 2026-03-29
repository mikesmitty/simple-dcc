#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Mock FreeRTOS before including project headers
#include "FreeRTOS.h"

// OpenLCB library types
#include "openlcb/openlcb_types.h"
#include "openlcb/openlcb_defines.h"
#include "drivers/canbus/can_types.h"
#include "openlcb/openlcb_gridconnect.h"
#include "openlcb/openlcb_config.h"
#include "drivers/canbus/can_config.h"

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

// Mock OpenLcb library functions
void OpenLcb_100ms_timer_tick(void) {}
void OpenLcb_initialize(const openlcb_config_t *config) {}
openlcb_node_t *OpenLcb_create_node(node_id_t id, const node_parameters_t *params) {
    static openlcb_node_t node;
    node.id = id;
    return &node;
}
uint16_t OpenLcbApplication_register_consumer_eventid(openlcb_node_t *openlcb_node, event_id_t event_id, event_status_enum event_status) { return 0; }
void OpenLcb_run(void) {}
openlcb_node_t *OpenLcbNode_find_by_node_id(node_id_t id) { return NULL; }

// Mock other deps
bool serial_write_ready(void) { return true; }
void serial_write(const uint8_t *buf, uint16_t len) {}
void OpenLcbGridConnect_from_can_msg(gridconnect_buffer_t *gc, can_msg_t *msg) {}
bool nv_storage_init(void *mem, size_t size) { return false; }
bool nv_storage_write(const void *mem, size_t size) { return true; }
void track_set_power(track_t *track, bool on) {}
void dcc_emergency_stop_all(dcc_engine_t *engine) {}
void CanConfig_initialize(const can_config_t *config) {}

// Global mock objects
motor_t mock_motor = { .state = 0 };
track_t mock_track = { .motor = &mock_motor };
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
    return MTI_VERIFY_NODE_ID_GLOBAL;
}

static void test_node_id_generation(void) {
    lcc_interface_init(NULL, &mock_track, NULL);
    
    node_id_t cs_id = g_cs_node->id;
    // board ID was 0x1122334455667788
    // get_unique_node_id uses last 4 bytes: 0x55667788
    // cs_id should be 0x060155667788
    assert((cs_id & 0xFFFF00000000ULL) == 0x060100000000ULL);
    assert((cs_id & 0x0000FFFFFFFFULL) == 0x000055667788ULL);

    node_id_t train_base = lcc_interface_get_train_node_id_base();
    // g_train_node_id_base = 0x060100000000ULL | (cs_id & 0x0000FFFF0000ULL);
    // cs_id = 0x060155667788
    // train_base = 0x060155660000
    assert(train_base == 0x060155660000ULL);

    printf("  PASS: node id generation\n");
}

static void test_auto_discovery_logic(void) {
    lcc_interface_init(NULL, &mock_track, NULL);
    
    can_msg_t msg;
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

int main(void) {
    printf("test_lcc_interface:\n");
    test_node_id_generation();
    test_auto_discovery_logic();
    printf("All LCC interface tests passed.\n");
    return 0;
}
