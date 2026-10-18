cmake_minimum_required(VERSION 3.13)

project(simple-dcc-tests C)

set(CMAKE_C_STANDARD 11)

# Host-side tests don't use Pico SDK or FreeRTOS.
# We compile just the pure-logic source files against host libc.

add_executable(test_packet
    test_packet.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/dcc/packet.c
)
target_include_directories(test_packet PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_priority_queue_logic
    test_priority_queue_logic.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/dcc/packet.c
)
target_include_directories(test_priority_queue_logic PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_functions
    test_functions.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/dcc/functions.c
)
target_include_directories(test_functions PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_lcc_traction
    test_lcc_traction.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/lcc/lcc_traction.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/lcc/lcc_float16.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/lcc/lcc_memconfig.c
)
target_include_directories(test_lcc_traction PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_lcc_memconfig
    test_lcc_memconfig.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/lcc/lcc_memconfig.c
)
target_include_directories(test_lcc_memconfig PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_lcc_datagram
    test_lcc_datagram.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/lcc/lcc_datagram.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/lcc/lcc_memconfig.c
)
target_include_directories(test_lcc_datagram PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_svc_ack
    test_svc_ack.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/servicemode/svc_ack.c
)
target_include_directories(test_svc_ack PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_svc_cv
    test_svc_cv.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/servicemode/svc_cv.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/dcc/packet.c
)
target_include_directories(test_svc_cv PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_lcc_interface
    test_lcc_interface.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/lcc/lcc_events.c
)
target_include_directories(test_lcc_interface PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_lcc_if_multitransport
    test_lcc_if_multitransport.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/lcc/lcc_gc.c
)
target_include_directories(test_lcc_if_multitransport PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_mcp2515_framing
    test_mcp2515_framing.c
)
target_include_directories(test_mcp2515_framing PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

enable_testing()
add_test(NAME test_packet COMMAND test_packet)
add_test(NAME test_priority_queue_logic COMMAND test_priority_queue_logic)
add_test(NAME test_functions COMMAND test_functions)
add_test(NAME test_lcc_traction COMMAND test_lcc_traction)
add_test(NAME test_lcc_memconfig COMMAND test_lcc_memconfig)
add_test(NAME test_lcc_datagram COMMAND test_lcc_datagram)
add_test(NAME test_svc_ack COMMAND test_svc_ack)
add_test(NAME test_svc_cv COMMAND test_svc_cv)
add_test(NAME test_lcc_interface COMMAND test_lcc_interface)
add_test(NAME test_lcc_if_multitransport COMMAND test_lcc_if_multitransport)
add_test(NAME test_mcp2515_framing COMMAND test_mcp2515_framing)
