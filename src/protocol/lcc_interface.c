#include "protocol/lcc_interface.h"
#include "protocol/lcc_traction.h"
#include "serial/serial.h"

#include "openlcb/openlcb_config.h"
#include "openlcb/openlcb_gridconnect.h"
#include "openlcb/openlcb_defines.h"
#include "openlcb/openlcb_application.h"
#include "openlcb/openlcb_node.h"
#include "drivers/canbus/can_config.h"
#include "drivers/canbus/can_types.h"
#include "drivers/canbus/can_utilities.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"

#include "pico/unique_id.h"
#include "util/dbg.h"
#include <string.h>

static SemaphoreHandle_t lcc_mutex;
dcc_engine_t *g_dcc_engine;
static track_t *g_track_main;
static openlcb_node_t *g_cs_node;

// Train node ID range: 0x060100000000 | dcc_address
#define TRAIN_NODE_ID_BASE 0x060100000000ULL

// --- Configuration memory (RAM-backed, space 0xFD) ---

#define CONFIG_OFFSET_AUTO_CLAIM  127   // after ACDI user fields (63 + 64)
#define CONFIG_MEM_SIZE           0x0200

static uint8_t cs_config_mem[CONFIG_MEM_SIZE];

static void config_mem_init_defaults(void) {
    memset(cs_config_mem, 0, CONFIG_MEM_SIZE);
    cs_config_mem[CONFIG_OFFSET_AUTO_CLAIM] = 1;  // default: enabled
}

// Pending train node creation (set by serial task, consumed by protocol task)
static volatile uint16_t pending_train_addr;

// --- CAN driver shim (GridConnect over USB CDC) ---

static bool can_transmit(can_msg_t *can_msg) {
    if (!serial_write_ready()) return false;

    gridconnect_buffer_t gc_buf;
    OpenLcbGridConnect_from_can_msg(&gc_buf, can_msg);

    // Write the GridConnect string including terminating newline
    uint16_t len = 0;
    while (gc_buf[len] != 0 && len < MAX_GRID_CONNECT_LEN) len++;
    serial_write(gc_buf, len);
    serial_write((const uint8_t *)"\n", 1);
    return true;
}

static bool can_is_tx_clear(void) {
    return serial_write_ready();
}

static void can_lock(void) {
    xSemaphoreTake(lcc_mutex, portMAX_DELAY);
}

static void can_unlock(void) {
    xSemaphoreGive(lcc_mutex);
}

// --- OpenLCB config memory stubs ---

static uint16_t config_mem_read(openlcb_node_t *node, uint32_t address,
                                uint16_t count, configuration_memory_buffer_t *buffer) {
    (void)node;
    if (address >= CONFIG_MEM_SIZE)
        return 0;
    if (address + count > CONFIG_MEM_SIZE)
        count = (uint16_t)(CONFIG_MEM_SIZE - address);
    memcpy(buffer, &cs_config_mem[address], count);
    return count;
}

static uint16_t config_mem_write(openlcb_node_t *node, uint32_t address,
                                 uint16_t count, configuration_memory_buffer_t *buffer) {
    (void)node;
    if (address >= CONFIG_MEM_SIZE)
        return 0;
    if (address + count > CONFIG_MEM_SIZE)
        count = (uint16_t)(CONFIG_MEM_SIZE - address);
    memcpy(&cs_config_mem[address], buffer, count);
    return count;
}

// --- Timer callback ---

static TimerHandle_t lcc_timer;

static void lcc_timer_callback(TimerHandle_t timer) {
    (void)timer;
    OpenLcb_100ms_timer_tick();
}

// --- Train node login-complete callback ---

static bool lcc_on_login_complete(openlcb_node_t *node) {
    // Only re-announce train nodes (not the command station)
    if (!node->train_state) return true;

    // Send Verified Node ID as a raw GridConnect frame so JMRI re-discovers
    // this node now that it can actually handle traction messages (RUNSTATE_RUN).
    can_msg_t vmsg;
    memset(&vmsg, 0, sizeof(vmsg));
    vmsg.identifier = RESERVED_TOP_BIT | CAN_OPENLCB_MSG
                    | OPENLCB_MESSAGE_STANDARD_FRAME_TYPE
                    | ((uint32_t)(MTI_VERIFIED_NODE_ID & 0x0FFF) << 12)
                    | node->alias;
    vmsg.payload_count = 6;
    vmsg.payload[0] = (node->id >> 40) & 0xFF;
    vmsg.payload[1] = (node->id >> 32) & 0xFF;
    vmsg.payload[2] = (node->id >> 24) & 0xFF;
    vmsg.payload[3] = (node->id >> 16) & 0xFF;
    vmsg.payload[4] = (node->id >> 8) & 0xFF;
    vmsg.payload[5] = node->id & 0xFF;

    gridconnect_buffer_t gc_buf;
    OpenLcbGridConnect_from_can_msg(&gc_buf, &vmsg);
    uint16_t len = 0;
    while (gc_buf[len] != 0 && len < MAX_GRID_CONNECT_LEN) len++;
    serial_write(gc_buf, len);
    serial_write((const uint8_t *)"\n", 1);

    DBG("[DBG] train node ready, re-announced alias=0x%03x\n", node->alias);
    return true;
}

// --- LCC well-known event handling (track power, global estop) ---

static void on_pc_event_report(openlcb_node_t *node, event_id_t *event_id) {
    DBG("[DBG] pc_event node=%p cs=%p event=%04x%08x\n",
           (void *)node, (void *)g_cs_node,
           (unsigned)((*event_id) >> 32),
           (unsigned)((*event_id) & 0xFFFFFFFF));

    // Only handle once (callback fires per node on the bus)
    if (node != g_cs_node) return;

    switch (*event_id) {
        case EVENT_ID_EMERGENCY_OFF:
            DBG("[DBG] EMERGENCY_OFF -> power off\n");
            track_set_power(g_track_main, false);
            dcc_emergency_stop_all(g_dcc_engine);
            break;
        case EVENT_ID_CLEAR_EMERGENCY_OFF:
            DBG("[DBG] CLEAR_EMERGENCY_OFF -> power on\n");
            track_set_power(g_track_main, true);
            break;
        case EVENT_ID_EMERGENCY_STOP:
            DBG("[DBG] EMERGENCY_STOP\n");
            dcc_emergency_stop_all(g_dcc_engine);
            break;
        case EVENT_ID_CLEAR_EMERGENCY_STOP:
            DBG("[DBG] CLEAR_EMERGENCY_STOP\n");
            break;
        default:
            break;
    }
}

// --- Node ID from hardware ---

static node_id_t get_unique_node_id(void) {
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    // Use last 4 bytes of the 8-byte board ID as the unique part
    // Set the top two bytes to the private range prefix (0x0601)
    node_id_t id = 0x060100000000ULL;
    id |= ((uint64_t)board_id.id[4] << 24);
    id |= ((uint64_t)board_id.id[5] << 16);
    id |= ((uint64_t)board_id.id[6] << 8);
    id |= ((uint64_t)board_id.id[7]);
    return id;
}

bool lcc_interface_auto_claim_enabled(void) {
    return cs_config_mem[CONFIG_OFFSET_AUTO_CLAIM] != 0;
}

void lcc_interface_init(dcc_engine_t *dcc, track_t *track, QueueHandle_t pqueue_input) {
    (void)pqueue_input;

    g_dcc_engine = dcc;
    g_track_main = track;
    config_mem_init_defaults();
    lcc_mutex = xSemaphoreCreateMutex();

    // Initialize CAN transport (must be before OpenLcb_initialize)
    static const can_config_t can_cfg = {
        .transmit_raw_can_frame  = can_transmit,
        .is_tx_buffer_clear      = can_is_tx_clear,
        .lock_shared_resources   = can_lock,
        .unlock_shared_resources = can_unlock,
        .on_rx                   = NULL,
        .on_tx                   = NULL,
        .on_alias_change         = NULL,
    };
    CanConfig_initialize(&can_cfg);

    // Initialize OpenLCB protocol stack
    static const openlcb_config_t olcb_cfg = {
        .lock_shared_resources   = can_lock,
        .unlock_shared_resources = can_unlock,
        .config_mem_read         = config_mem_read,
        .config_mem_write        = config_mem_write,
        .reboot                  = NULL,
        .factory_reset           = NULL,

        .on_login_complete            = lcc_on_login_complete,
        .on_pc_event_report           = on_pc_event_report,

        .on_train_speed_changed       = lcc_traction_on_speed_changed,
        .on_train_function_changed    = lcc_traction_on_function_changed,
        .on_train_emergency_entered   = lcc_traction_on_emergency_entered,
        .on_train_emergency_exited    = lcc_traction_on_emergency_exited,
        .on_train_search_no_match     = lcc_traction_on_search_no_match,
    };
    OpenLcb_initialize(&olcb_cfg);

    // Create the command station node
    extern const node_parameters_t OpenLcbUserConfig_node_parameters;
    node_id_t cs_id = get_unique_node_id();
    g_cs_node = OpenLcb_create_node(cs_id, &OpenLcbUserConfig_node_parameters);

    // Register CS as consumer of well-known emergency events
    OpenLcbApplication_register_consumer_eventid(g_cs_node, EVENT_ID_EMERGENCY_OFF, EVENT_STATUS_SET);
    OpenLcbApplication_register_consumer_eventid(g_cs_node, EVENT_ID_CLEAR_EMERGENCY_OFF, EVENT_STATUS_CLEAR);
    OpenLcbApplication_register_consumer_eventid(g_cs_node, EVENT_ID_EMERGENCY_STOP, EVENT_STATUS_SET);
    OpenLcbApplication_register_consumer_eventid(g_cs_node, EVENT_ID_CLEAR_EMERGENCY_STOP, EVENT_STATUS_CLEAR);

    // Enable track power at startup (standard CS behavior)
    track_set_power(g_track_main, true);
    DBG("[INIT] track power enabled, motor state=%d\n", g_track_main->motor->state);

    // Start 100ms timer
    lcc_timer = xTimerCreate("lcc_tick", pdMS_TO_TICKS(100),
                             pdTRUE, NULL, lcc_timer_callback);
    xTimerStart(lcc_timer, 0);
}

// --- Auto-create train nodes on Verify Node ID Global ---

void lcc_interface_on_rx_can_msg(can_msg_t *msg) {
    uint16_t mti = CanUtilities_convert_can_mti_to_openlcb_mti(msg);
    if (mti != MTI_VERIFY_NODE_ID_GLOBAL || msg->payload_count < 6)
        return;

    if (!lcc_interface_auto_claim_enabled())
        return;

    node_id_t node_id = CanUtilities_extract_can_payload_as_node_id(msg);
    if ((node_id & 0xFFFFFF000000ULL) != TRAIN_NODE_ID_BASE)
        return;

    uint16_t addr = (uint16_t)(node_id & 0xFFFF);
    if (addr == 0)
        return;

    DBG("[RX] verify_node_id for train addr=%u\n", addr);
    pending_train_addr = addr;
}

static void process_pending_train_node(void) {
    uint16_t addr = pending_train_addr;
    if (addr == 0) return;
    pending_train_addr = 0;

    node_id_t train_id = TRAIN_NODE_ID_BASE | (uint64_t)addr;
    if (OpenLcbNode_find_by_node_id(train_id)) {
        DBG("[DBG] train node addr=%u already exists\n", addr);
        return;
    }

    DBG("[DBG] auto-creating train node addr=%u\n", addr);
    lcc_traction_on_search_no_match(addr, 0);
}

void task_protocol(void *params) {
    (void)params;

    for (;;) {
        process_pending_train_node();
        OpenLcb_run();
        vTaskDelay(1);
    }
}
