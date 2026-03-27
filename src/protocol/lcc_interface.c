#include "protocol/lcc_interface.h"
#include "protocol/lcc_traction.h"
#include "serial/serial.h"

#include "openlcb/openlcb_config.h"
#include "openlcb/openlcb_gridconnect.h"
#include "drivers/canbus/can_config.h"
#include "drivers/canbus/can_types.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"

#include "pico/unique_id.h"
#include <string.h>

static SemaphoreHandle_t lcc_mutex;
dcc_engine_t *g_dcc_engine;

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
    (void)node; (void)address;
    memset(buffer, 0, count);
    return count;
}

static uint16_t config_mem_write(openlcb_node_t *node, uint32_t address,
                                 uint16_t count, configuration_memory_buffer_t *buffer) {
    (void)node; (void)address; (void)count; (void)buffer;
    return 0; // TODO: implement flash-backed config memory
}

// --- Timer callback ---

static TimerHandle_t lcc_timer;

static void lcc_timer_callback(TimerHandle_t timer) {
    (void)timer;
    OpenLcb_100ms_timer_tick();
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

void lcc_interface_init(dcc_engine_t *dcc, QueueHandle_t pqueue_input) {
    (void)pqueue_input;

    g_dcc_engine = dcc;
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
    OpenLcb_create_node(cs_id, &OpenLcbUserConfig_node_parameters);

    // Start 100ms timer
    lcc_timer = xTimerCreate("lcc_tick", pdMS_TO_TICKS(100),
                             pdTRUE, NULL, lcc_timer_callback);
    xTimerStart(lcc_timer, 0);
}

void task_protocol(void *params) {
    (void)params;

    for (;;) {
        OpenLcb_run();
        vTaskDelay(1);
    }
}
