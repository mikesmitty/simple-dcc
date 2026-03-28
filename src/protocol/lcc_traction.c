#include "protocol/lcc_traction.h"
#include "protocol/lcc_interface.h"
#include "dcc/dcc.h"
#include "openlcb/openlcb_float16.h"
#include "openlcb/openlcb_config.h"
#include "openlcb/openlcb_application_train.h"

#include <math.h>
#include "util/dbg.h"

// Set by lcc_interface_init
extern dcc_engine_t *g_dcc_engine;

static uint16_t train_dcc_address(openlcb_node_t *node) {
    if (node->train_state) {
        return node->train_state->dcc_address;
    }
    return 0;
}

void lcc_traction_on_speed_changed(openlcb_node_t *node, uint16_t speed_float16) {
    uint16_t addr = train_dcc_address(node);
    DBG("[DBG] speed_changed addr=%u f16=0x%04x\n", addr, speed_float16);
    if (addr == 0) return;

    float speed_f = OpenLcbFloat16_get_speed(speed_float16);
    bool reverse = OpenLcbFloat16_get_direction(speed_float16);

    // Convert float speed (m/s) to 128-step DCC speed
    // LCC speed is 0.0 .. ~126.0 (meters per second equivalent)
    // DCC 128-step: 0=stop, 1=estop, 2-127=speed
    uint8_t speed_step;
    if (OpenLcbFloat16_is_nan(speed_float16)) {
        speed_step = 1; // estop
    } else if (OpenLcbFloat16_is_zero(speed_float16) || speed_f <= 0.0f) {
        speed_step = 0; // stop
    } else {
        // Map float to 2-127 range
        speed_step = (uint8_t)(speed_f * 126.0f / 126.8f) + 2;
        if (speed_step > 127) speed_step = 127;
    }

    DBG("[DBG] -> dcc addr=%u step=%u dir=%c\n", addr, speed_step, reverse ? 'R' : 'F');
    dcc_set_throttle(g_dcc_engine, addr, speed_step, !reverse);
}

void lcc_traction_on_function_changed(openlcb_node_t *node, uint32_t fn_addr, uint16_t fn_value) {
    uint16_t addr = train_dcc_address(node);
    if (addr == 0) return;

    dcc_set_function(g_dcc_engine, addr, (uint16_t)fn_addr, fn_value != 0);
}

void lcc_traction_on_emergency_entered(openlcb_node_t *node, train_emergency_type_enum emergency_type) {
    switch (emergency_type) {
        case TRAIN_EMERGENCY_TYPE_GLOBAL_OFF:
        case TRAIN_EMERGENCY_TYPE_GLOBAL_STOP:
            // Handled by on_pc_event_report at the command station level
            return;
        case TRAIN_EMERGENCY_TYPE_ESTOP: {
            uint16_t addr = train_dcc_address(node);
            if (addr == 0) return;
            dcc_emergency_stop(g_dcc_engine, addr);
            break;
        }
    }
}

void lcc_traction_on_emergency_exited(openlcb_node_t *node, train_emergency_type_enum emergency_type) {
    (void)emergency_type;
    (void)node;
    // Track power restore handled by on_pc_event_report at CS level.
    // Point-to-point estop clear: next speed command resumes the loco.
}

openlcb_node_t *lcc_traction_on_search_no_match(uint16_t search_address, uint8_t flags) {
    (void)flags;

    DBG("[DBG] search_no_match addr=%u flags=0x%02x\n", search_address, flags);

    if (!lcc_interface_auto_claim_enabled())
        return NULL;

    // Create a new train node for this address
    // Node ID: base + address offset (use a well-known range)
    // The library will handle alias negotiation
    extern const node_parameters_t OpenLcbUserConfig_train_node_parameters;

    // Create a node ID by embedding the DCC address
    // Using a private range: 0x060100000000 + address
    node_id_t train_node_id = 0x060100000000ULL | (uint64_t)search_address;

    openlcb_node_t *node = OpenLcb_create_node(train_node_id, &OpenLcbUserConfig_train_node_parameters);
    if (!node) {
        DBG("[DBG] search_no_match: node alloc FAILED\n");
        return NULL;
    }

    // Allocate train state from the library's pool
    OpenLcbApplicationTrain_setup(node);

    // Set up train state with the DCC address
    if (node->train_state) {
        node->train_state->dcc_address = search_address;
        node->train_state->is_long_address = (search_address > 127);
        node->train_state->speed_steps = 3; // 128-step mode
        DBG("[DBG] train node created addr=%u node_id=%012llx\n",
               search_address, (unsigned long long)train_node_id);
    } else {
        DBG("[DBG] search_no_match: train_state is NULL!\n");
    }

    // Create corresponding loco in DCC engine
    dcc_get_or_create_loco(g_dcc_engine, search_address);

    return node;
}
