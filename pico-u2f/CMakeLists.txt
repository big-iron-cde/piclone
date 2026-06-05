cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")

include(${CMAKE_CURRENT_LIST_DIR}/../../picoprom/pico_sdk_import.cmake)

project(pico-u2f C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico-u2f
    main.c
    usb_descriptors.c
    u2f_hid.c
    u2f.c
    sha256.c
    uECC.c
)

target_include_directories(pico-u2f PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(pico-u2f
    pico_stdlib
    pico_rand
    hardware_gpio
    hardware_sync
    tinyusb_device
    tinyusb_board
)

pico_add_extra_outputs(pico-u2f)

# Disable stdio USB so we own TinyUSB fully
pico_enable_stdio_usb(pico-u2f 0)
pico_enable_stdio_uart(pico-u2f 0)
