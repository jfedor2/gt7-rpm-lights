cmake_minimum_required(VERSION 3.13)

add_compile_definitions(PICO_DEFAULT_UART_BAUD_RATE=921600)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk")

set(PICO_BOARD pico_w)

include(pico_sdk_import.cmake)

project(rpm)

pico_sdk_init()

add_executable(rpm
    src/rpm.c
    src/salsa20.c
    src/ws2812.c
    src/crc.c
    src/tinyusb_stuff.c
)
pico_generate_pio_header(rpm ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)
target_include_directories(rpm PRIVATE src)
target_link_libraries(rpm
    pico_stdlib
    tinyusb_device
    tinyusb_board
    pico_cyw43_arch_lwip_poll
)
pico_add_extra_outputs(rpm)
