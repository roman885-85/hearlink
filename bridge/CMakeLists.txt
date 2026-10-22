cmake_minimum_required(VERSION 3.13)
# Міст набору «Слух» для Raspberry Pi Pico: програма (bridge) і завантажувач (loader). Збирання — ./build.sh.
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(hearlink_bridge C CXX ASM)
set(CMAKE_C_STANDARD 11)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
pico_sdk_init()

add_subdirectory(lib/pico_spdif_rx)

# ---- програма моста (з 0x10008000)
add_executable(bridge src/main.c src/asrc.c)
pico_generate_pio_header(bridge ${CMAKE_CURRENT_LIST_DIR}/src/bridge.pio)
target_include_directories(bridge PRIVATE src ${CMAKE_CURRENT_LIST_DIR}/../hearlink)
target_link_libraries(bridge pico_stdlib pico_multicore hardware_pio hardware_dma hardware_uart hardware_watchdog
                      hardware_clocks hardware_pwm pico_spdif_rx)
# PICO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK=1: «picotool load -f» переводить Pico в режим запису БЕЗ диска
# RPI-RP2 — на цьому Mac (macOS 15, FSKit msdos) монтування того диска вішало систему («Bad volume», завис Dock).
# Ядру 1 — стек 4 КБ (переривання прийому AES ідуть на його стеку).
target_compile_definitions(bridge PRIVATE PICO_SPDIF_RX_PIO=0 PICO_SPDIF_RX_DMA_IRQ=0
                           PICO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK=1 PICO_CORE1_STACK_SIZE=0x1000
                           PICO_PANIC_FUNCTION=bridge_panic PICO_PANIC_FUNCTION_DOES_NOT_RETURN=1
                           SYS_CLK_HZ=200000000)   # 200 МГц — з SDK 2.1 офіційно для RP2040 (напруга 1,15 В — сама)
target_compile_options(bridge PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_source_files_properties(src/asrc.c PROPERTIES COMPILE_OPTIONS "-O2")
pico_enable_stdio_uart(bridge 0)
pico_enable_stdio_usb(bridge 1)   # стан і команди на столі; через нього ж picotool переводить Pico в режим запису
pico_add_linker_script_override_path(bridge ${CMAKE_CURRENT_LIST_DIR}/ld/app FILES pico_flash_region.ld section_boot2.incl)
pico_add_extra_outputs(bridge)

# ---- завантажувач (0x10000000, до 28 КБ)
add_executable(loader loader/main.c)
target_include_directories(loader PRIVATE src ${CMAKE_CURRENT_LIST_DIR}/../hearlink)
target_link_libraries(loader pico_stdlib hardware_uart hardware_flash hardware_watchdog)
target_compile_definitions(loader PRIVATE PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1)
target_compile_options(loader PRIVATE -Wall -Wextra -Os)
pico_enable_stdio_uart(loader 0)
pico_enable_stdio_usb(loader 0)
pico_add_linker_script_override_path(loader ${CMAKE_CURRENT_LIST_DIR}/ld/loader FILES pico_flash_region.ld)
pico_add_extra_outputs(loader)

# ---- перевірка плати: лише в ОЗУ (picotool load -x -f build/board_probe.uf2), з усім не збирається — ninja board_probe
add_executable(board_probe EXCLUDE_FROM_ALL test/board_probe.c)
pico_set_binary_type(board_probe no_flash)
target_link_libraries(board_probe pico_stdlib hardware_adc)
target_compile_definitions(board_probe PRIVATE PICO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK=1)
pico_enable_stdio_uart(board_probe 0)
pico_enable_stdio_usb(board_probe 1)
pico_add_extra_outputs(board_probe)

# ---- для першої записи: один файл (завантажувач + відомості + програма) і образ для передавача
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/bridge-full.uf2 ${CMAKE_CURRENT_LIST_DIR}/../hearlink/bridge_fw.h
    DEPENDS bridge loader ${CMAKE_CURRENT_LIST_DIR}/tools/make_images.py ${CMAKE_CURRENT_LIST_DIR}/src/version.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/make_images.py
            ${CMAKE_BINARY_DIR}/loader.bin ${CMAKE_BINARY_DIR}/bridge.bin ${CMAKE_CURRENT_LIST_DIR}/src/version.h
            ${CMAKE_BINARY_DIR}/bridge-full.uf2 ${CMAKE_CURRENT_LIST_DIR}/../hearlink/bridge_fw.h
)
add_custom_target(images ALL DEPENDS ${CMAKE_BINARY_DIR}/bridge-full.uf2)
