cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pico_blink C CXX ASM)

pico_sdk_init()

add_executable(pico2_fw blink.c usb_descriptors.c)
target_include_directories(pico2_fw PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)



target_link_libraries(pico2_fw
    pico_stdlib
    tinyusb_device
)

pico_enable_stdio_uart(pico2_fw 0)

pico_add_extra_outputs(pico2_fw)
