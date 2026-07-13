cmake_minimum_required(VERSION 3.13)

# IMPORTANT: PICO_BOARD must be set BEFORE include(pico_sdk_import.cmake)
# Pico 2 on the breadboard build; use "pico" for original RP2040 Pico.
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Locate SDK via PICO_SDK_PATH, or: cmake -DPICO_SDK_PATH=~/vsarm/pico-sdk ..
include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)

project(piclone C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init() 

add_executable(piclone
    main.c
    protocol.c
    hardware_api.c
    json_util.c
    third_party/cJSON/cJSON.c
)

target_include_directories(piclone PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/third_party/cJSON
)

set(STD_LIBS
    pico_stdlib
    hardware_gpio
    hardware_clocks
    hardware_sync
)

if(PICO_BOARD STREQUAL "pico_w" OR PICO_BOARD STREQUAL "pico2_w")
    list(APPEND STD_LIBS pico_cyw43_arch_none)
endif()

target_link_libraries(piclone
   PRIVATE ${STD_LIBS}
)

pico_add_extra_outputs(piclone)

pico_enable_stdio_usb(piclone 1)
pico_enable_stdio_uart(piclone 0)
