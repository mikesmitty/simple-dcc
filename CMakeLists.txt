cmake_minimum_required(VERSION 3.13)

# Board and platform must be set before importing SDK
set(PICO_BOARD pico2 CACHE STRING "Target board")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Target platform")

# Default to submodule paths; env vars override if set
if (NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk)
endif()
if (NOT FREERTOS_KERNEL_PATH AND NOT DEFINED ENV{FREERTOS_KERNEL_PATH})
    set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/FreeRTOS-Kernel)
endif()

include(pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)

project(simple-dcc C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# --- OpenLcbCLib ---
set(OPENLCB_DIR ${CMAKE_CURRENT_LIST_DIR}/lib/OpenLcbCLib/src)

add_library(openlcb
    ${OPENLCB_DIR}/openlcb/openlcb_application.c
    ${OPENLCB_DIR}/openlcb/openlcb_buffer_fifo.c
    ${OPENLCB_DIR}/openlcb/openlcb_buffer_list.c
    ${OPENLCB_DIR}/openlcb/openlcb_buffer_store.c
    ${OPENLCB_DIR}/openlcb/openlcb_gridconnect.c
    ${OPENLCB_DIR}/openlcb/openlcb_login_statemachine_handler.c
    ${OPENLCB_DIR}/openlcb/openlcb_login_statemachine.c
    ${OPENLCB_DIR}/openlcb/openlcb_main_statemachine.c
    ${OPENLCB_DIR}/openlcb/openlcb_node.c
    ${OPENLCB_DIR}/openlcb/openlcb_utilities.c
    ${OPENLCB_DIR}/openlcb/openlcb_float16.c
    ${OPENLCB_DIR}/openlcb/openlcb_config.c
    ${OPENLCB_DIR}/openlcb/protocol_datagram_handler.c
    ${OPENLCB_DIR}/openlcb/protocol_event_transport.c
    ${OPENLCB_DIR}/openlcb/protocol_message_network.c
    ${OPENLCB_DIR}/openlcb/protocol_snip.c
    ${OPENLCB_DIR}/openlcb/protocol_config_mem_read_handler.c
    ${OPENLCB_DIR}/openlcb/protocol_config_mem_write_handler.c
    ${OPENLCB_DIR}/openlcb/protocol_config_mem_operations_handler.c
    ${OPENLCB_DIR}/openlcb/protocol_broadcast_time_handler.c
    ${OPENLCB_DIR}/openlcb/openlcb_application_broadcast_time.c
    ${OPENLCB_DIR}/openlcb/protocol_train_handler.c
    ${OPENLCB_DIR}/openlcb/openlcb_application_train.c
    ${OPENLCB_DIR}/openlcb/protocol_train_search_handler.c
    ${OPENLCB_DIR}/drivers/canbus/can_buffer_fifo.c
    ${OPENLCB_DIR}/drivers/canbus/can_buffer_store.c
    ${OPENLCB_DIR}/drivers/canbus/can_rx_message_handler.c
    ${OPENLCB_DIR}/drivers/canbus/can_login_message_handler.c
    ${OPENLCB_DIR}/drivers/canbus/can_tx_message_handler.c
    ${OPENLCB_DIR}/drivers/canbus/can_main_statemachine.c
    ${OPENLCB_DIR}/drivers/canbus/can_rx_statemachine.c
    ${OPENLCB_DIR}/drivers/canbus/can_login_statemachine.c
    ${OPENLCB_DIR}/drivers/canbus/can_tx_statemachine.c
    ${OPENLCB_DIR}/drivers/canbus/can_utilities.c
    ${OPENLCB_DIR}/drivers/canbus/can_config.c
    ${OPENLCB_DIR}/drivers/canbus/alias_mappings.c
    ${OPENLCB_DIR}/drivers/canbus/alias_mapping_listener.c
)

target_include_directories(openlcb PUBLIC
    ${OPENLCB_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src  # for openlcb_user_config.h, can_user_config.h
)

# --- Main application ---
add_executable(simple-dcc
    src/main.c
    src/openlcb_user_config.c
    src/dcc/packet.c
    src/dcc/dcc.c
    src/dcc/functions.c
    src/wavegen/wavegen.c
    src/queue/priority_queue.c
    src/serial/serial.c
    src/protocol/lcc_interface.c
    src/protocol/lcc_traction.c
    src/motor/motor.c
    src/track/track.c
    src/util/event_bus.c
)

# --- PIO programs (must come after add_executable) ---
pico_generate_pio_header(simple-dcc ${CMAKE_CURRENT_LIST_DIR}/src/wavegen/wavegen.pio)
pico_generate_pio_header(simple-dcc ${CMAKE_CURRENT_LIST_DIR}/src/wavegen/wavegen_nocutout.pio)
pico_generate_pio_header(simple-dcc ${CMAKE_CURRENT_LIST_DIR}/src/wavegen/wavegen_servicemode.pio)
pico_generate_pio_header(simple-dcc ${CMAKE_CURRENT_LIST_DIR}/src/wavegen/cutout.pio)

target_include_directories(simple-dcc PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(simple-dcc PRIVATE
    pico_stdlib
    hardware_adc
    hardware_pio
    FreeRTOS-Kernel-Heap4
    openlcb
)

# USB CDC for serial I/O, disable UART stdio, enable RTT for debug
pico_enable_stdio_usb(simple-dcc 1)
pico_enable_stdio_uart(simple-dcc 0)
pico_enable_stdio_rtt(simple-dcc 1)

# Wait up to 2s during stdio_usb_init() for USB enumeration (bare-metal phase
# where the SDK's alarm-based tud_task() background IRQ works reliably).
target_compile_definitions(simple-dcc PRIVATE
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=2000
)

# Generate UF2/BIN/HEX outputs
pico_add_extra_outputs(simple-dcc)
