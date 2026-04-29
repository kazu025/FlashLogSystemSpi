cmake_minimum_required(VERSION 3.14)

include($ENV{PICO_SDK_PATH}/pico_sdk_import.cmake)

project(event_logger C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

include(FreeRTOS_Kernel_import.cmake)

add_executable(event_logger
    src/main.cpp
    src/EventLogger.cpp
    src/led25.c
    src/freertos_hooks.c
    src/LogEvent.cpp
    src/UartDma.cpp
    src/FlashLogStorage.cpp
    src/FlashDriver.cpp
    src/Crc32.cpp
    src/DebugUtils.cpp
    check/check.cpp
)

target_include_directories(event_logger PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/check
)

target_link_libraries(event_logger
    pico_stdlib
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    hardware_timer
	hardware_uart
    hardware_dma
    hardware_irq
	hardware_gpio
    hardware_spi
)
if (PICO_CYW43_SUPPORTED)
    target_link_libraries(event_logger pico_cyw43_arch_none)
endif()

pico_enable_stdio_usb(event_logger 1)
pico_enable_stdio_uart(event_logger 0)

pico_add_extra_outputs(event_logger)