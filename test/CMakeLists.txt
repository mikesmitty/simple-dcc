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
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_priority_queue_logic
    test_priority_queue_logic.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/dcc/packet.c
)
target_include_directories(test_priority_queue_logic PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

add_executable(test_functions
    test_functions.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/dcc/functions.c
)
target_include_directories(test_functions PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../src
)

enable_testing()
add_test(NAME test_packet COMMAND test_packet)
add_test(NAME test_priority_queue_logic COMMAND test_priority_queue_logic)
add_test(NAME test_functions COMMAND test_functions)
