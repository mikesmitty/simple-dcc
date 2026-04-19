# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
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

# --- Version string (release-please + git-describe) ---
# x-release-please-start-version
set(PROJECT_VERSION_BASE "0.4.0")
# x-release-please-end

execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# If git describe returned only a hash (shallow clone / untagged),
# prepend the base version so SNIP doesn't advertise a bare sha.
if (GIT_VERSION MATCHES "^[0-9a-f]+(-dirty)?$")
    set(GIT_VERSION "${PROJECT_VERSION_BASE}-${GIT_VERSION}")
endif()

if (NOT GIT_VERSION)
    set(GIT_VERSION "${PROJECT_VERSION_BASE}-unknown")
endif()

# --- CDI XML → C byte array ---
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cdi_data.c ${CMAKE_CURRENT_BINARY_DIR}/cdi_data.h
    COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/tools/XmlToArray.py --header -t cdi -o ${CMAKE_CURRENT_BINARY_DIR}/cdi_data ${CMAKE_CURRENT_LIST_DIR}/src/cdi.xml
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/src/cdi.xml
    COMMENT "Generating cdi_data.c and cdi_data.h from cdi.xml"
)

# --- LCC stack (replaces OpenLcbCLib) ---
add_library(lcc STATIC
    src/lcc/lcc_frame.c
    src/lcc/lcc_gc.c
    src/lcc/lcc_if.c
    src/lcc/lcc_task.c
    src/lcc/lcc_node.c
    src/lcc/lcc_alias.c
    src/lcc/lcc_pip.c
    src/lcc/lcc_snip.c
    src/lcc/lcc_datagram.c
    src/lcc/lcc_memconfig.c
    src/lcc/lcc_events.c
    src/lcc/lcc_float16.c
    src/lcc/lcc_traction.c
    ${CMAKE_CURRENT_BINARY_DIR}/cdi_data.c
)
target_include_directories(lcc PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(lcc PUBLIC
    pico_stdlib
    FreeRTOS-Kernel-Heap4
)

# --- Main application ---
add_executable(simple-dcc
    src/main.c
    src/dcc/packet.c
    src/dcc/dcc.c
    src/dcc/functions.c
    src/wavegen/wavegen.c
    src/queue/priority_queue.c
    src/serial/serial.c
    src/protocol/lcc_interface.c
    src/motor/motor.c
    src/track/track.c
    src/util/event_bus.c
    src/util/nv_storage.c
)

# --- PIO programs (must come after add_executable) ---
pico_generate_pio_header(simple-dcc ${CMAKE_CURRENT_LIST_DIR}/src/wavegen/wavegen.pio)
pico_generate_pio_header(simple-dcc ${CMAKE_CURRENT_LIST_DIR}/src/wavegen/wavegen_nocutout.pio)
pico_generate_pio_header(simple-dcc ${CMAKE_CURRENT_LIST_DIR}/src/wavegen/wavegen_servicemode.pio)
pico_generate_pio_header(simple-dcc ${CMAKE_CURRENT_LIST_DIR}/src/wavegen/cutout.pio)

target_include_directories(simple-dcc PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(simple-dcc PRIVATE
    pico_stdlib
    hardware_adc
    hardware_pio
    hardware_flash
    hardware_sync
    pico_flash
    pico_multicore
    FreeRTOS-Kernel-Heap4
    lcc
)

# USB CDC for serial I/O, disable UART stdio, enable RTT for debug
pico_enable_stdio_usb(simple-dcc 1)
pico_enable_stdio_uart(simple-dcc 0)
pico_enable_stdio_rtt(simple-dcc 1)

# Wait up to 2s during stdio_usb_init() for USB enumeration (bare-metal phase
# where the SDK's alarm-based tud_task() background IRQ works reliably).
target_compile_definitions(simple-dcc PRIVATE
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=2000
    USBD_MANUFACTURER="simple-dcc"
    USBD_PRODUCT="DCC Command Station"
    LCC_SW_VERSION="${GIT_VERSION}"
)

pico_set_program_name(simple-dcc "simple-dcc")
pico_set_program_version(simple-dcc "${GIT_VERSION}")

# Generate UF2/BIN/HEX outputs
pico_add_extra_outputs(simple-dcc)
