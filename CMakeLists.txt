cmake_minimum_required(VERSION 3.14)

include($ENV{PICO_SDK_PATH}/pico_sdk_import.cmake)

project(event_logger C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

include(FreeRTOS_Kernel_import.cmake)
add_executable(${PROJECT_NAME}
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
src/task_command.cpp
check/check.cpp
)

target_compile_definitions(${PROJECT_NAME} PRIVATE RUN_NORMAL_APP=1)
target_compile_definitions(${PROJECT_NAME} PRIVATE RUN_PSEUDO_POWER_CUT_TEST=0)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/check
)

target_link_libraries(${PROJECT_NAME}
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
    target_link_libraries(${PROJECT_NAME} pico_cyw43_arch_none)
endif()

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})