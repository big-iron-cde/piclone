cmake_minimum_required(VERSION 3.13)

# IMPORTANT: PICO_BOARD must be set BEFORE include(pico_sdk_import.cmake)
# Build for the Pi Pico 2 (RP2350). If you have a Pico 1 (RP2040) instead,
# change "pico2" to "pico" or just delete this line.
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Use the same SDK import shim that picoprom uses.
include(${CMAKE_CURRENT_LIST_DIR}/../../picoprom/pico_sdk_import.cmake)

project(pico-rom-test C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico-rom-test
    main.c
)

target_link_libraries(pico-rom-test
    pico_stdlib
    hardware_gpio
    hardware_clocks
    hardware_sync
)

pico_add_extra_outputs(pico-rom-test)

pico_enable_stdio_usb(pico-rom-test 1)
pico_enable_stdio_uart(pico-rom-test 0)
