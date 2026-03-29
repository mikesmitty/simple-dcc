#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Mock FreeRTOS and other dependencies before including project headers
#include "FreeRTOS.h"

// OpenLCB library types
#include "openlcb/openlcb_types.h"
#include "openlcb/openlcb_defines.h"

// Project headers with mocks
#include "dcc/dcc.h"

// Track calls to mocks
typedef struct {
    uint16_t addr;
    uint8_t speed;
    bool forward;
    int call_count;
} throttle_mock_t;

static throttle_mock_t last_throttle;

// DCC mocks
void dcc_set_throttle(dcc_engine_t *engine, uint16_t addr, uint8_t speed, bool forward) {
    last_throttle.addr = addr;
    last_throttle.speed = speed;
    last_throttle.forward = forward;
    last_throttle.call_count++;
}

void dcc_set_function(dcc_engine_t *engine, uint16_t addr, uint16_t fn_addr, bool on) {}
void dcc_emergency_stop(dcc_engine_t *engine, uint16_t addr) {}
loco_state_t *dcc_get_or_create_loco(dcc_engine_t *engine, uint16_t addr) { return NULL; }

// lcc_interface mocks
bool lcc_interface_auto_claim_enabled(void) { return true; }
node_id_t lcc_interface_get_train_node_id_base(void) { return 0x060112340000ULL; }

const node_parameters_t OpenLcbUserConfig_train_node_parameters = {0};

// OpenLcb library mocks
train_state_t* OpenLcbApplicationTrain_setup(openlcb_node_t *node) { return NULL; }
openlcb_node_t *OpenLcbNode_find_by_node_id(node_id_t node_id) { return NULL; }
openlcb_node_t *OpenLcb_create_node(node_id_t node_id, const node_parameters_t *params) {
    static openlcb_node_t mock_node;
    static train_state_t mock_train_state;
    mock_node.id = node_id;
    mock_node.train_state = &mock_train_state;
    return &mock_node;
}

// Float16 helpers (simplified for test)
#include "openlcb/openlcb_float16.h"
float OpenLcbFloat16_get_speed(uint16_t speed_float16) {
    return (float)(speed_float16 & 0x7FFF);
}
bool OpenLcbFloat16_get_direction(uint16_t speed_float16) {
    return (speed_float16 & 0x8000) != 0;
}
bool OpenLcbFloat16_is_nan(uint16_t speed_float16) { return speed_float16 == 0x7C01; }
bool OpenLcbFloat16_is_zero(uint16_t speed_float16) { return (speed_float16 & 0x7FFF) == 0; }

dcc_engine_t *g_dcc_engine = (dcc_engine_t *)0x1234;

// Include the source to be tested
#include "../src/protocol/lcc_traction.c"

static void test_speed_conversion(void) {
    memset(&last_throttle, 0, sizeof(last_throttle));
    
    train_state_t state = { .dcc_address = 3 };
    openlcb_node_t node = { .train_state = &state };

    // 1. Stop (0.0)
    lcc_traction_on_speed_changed(&node, 0x0000);
    assert(last_throttle.speed == 0);
    assert(last_throttle.forward == true);

    // 2. Forward speed
    lcc_traction_on_speed_changed(&node, 64); 
    assert(last_throttle.speed > 0);
    assert(last_throttle.forward == true);

    // 3. Reverse speed
    lcc_traction_on_speed_changed(&node, 0x8000 | 64);
    assert(last_throttle.forward == false);

    // 4. E-stop (NaN)
    lcc_traction_on_speed_changed(&node, 0x7C01);
    assert(last_throttle.speed == 1);

    printf("  PASS: speed conversion\n");
}

static void test_search_logic(void) {
    // Test short address
    openlcb_node_t *node = lcc_traction_on_search_no_match(3, 0);
    assert(node != NULL);
    assert(node->id == (0x060112340000ULL | 3));
    assert(node->train_state->dcc_address == 3);
    assert(node->train_state->is_long_address == false);

    // Test long address via flag
    node = lcc_traction_on_search_no_match(3, TRAIN_SEARCH_FLAG_LONG_ADDR);
    assert(node->train_state->is_long_address == true);

    // Test automatic long address (> 127)
    node = lcc_traction_on_search_no_match(1234, 0);
    assert(node->train_state->is_long_address == true);

    printf("  PASS: search logic\n");
}

int main(void) {
    printf("test_lcc_traction:\n");
    test_speed_conversion();
    test_search_logic();
    printf("All LCC traction tests passed.\n");
    return 0;
}
