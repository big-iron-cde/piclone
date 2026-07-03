cmake_minimum_required(VERSION 3.13)

# IMPORTANT: PICO_BOARD must be set BEFORE include(pico_sdk_import.cmake)
# Pico 1 (RP2040).
set(PICO_BOARD pico CACHE STRING "Board type")

# Use the same SDK import shim that picoprom uses.
include(${CMAKE_CURRENT_LIST_DIR}/../../picoprom/pico_sdk_import.cmake)

project(piclone C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(piclone
    main.c
    protocol.c
    hardware_api.c
)

target_link_libraries(piclone
    pico_stdlib
    hardware_gpio
    hardware_clocks
    hardware_sync
)

pico_add_extra_outputs(piclone)

pico_enable_stdio_usb(piclone 1)
pico_enable_stdio_uart(piclone 0)
