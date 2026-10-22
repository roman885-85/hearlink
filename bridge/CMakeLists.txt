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
# USB (з 22.10): порт і звукова карта (src/usb_audio.c, src/tusb_config.h) — описувачі свої, а запуск TinyUSB і фонова
# задача USB — від SDK, як і раніше; інтерфейс перезапуску (picotool) — драйвер SDK pico_usb_reset.
# PICO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK=1: «picotool load -f» переводить Pico в режим запису БЕЗ диска
# RPI-RP2 — на цьому Mac (macOS 15, FSKit msdos) монтування того диска вішало систему («Bad volume», завис Dock).
# Ядру 1 — стек 4 КБ (переривання прийому AES ідуть на його стеку).
set(BRIDGE_SRC src/main.c src/asrc.c src/usb_audio.c)
function(bridge_setup t)
    pico_generate_pio_header(${t} ${CMAKE_CURRENT_LIST_DIR}/src/bridge.pio)
    target_include_directories(${t} PRIVATE src ${CMAKE_CURRENT_LIST_DIR}/../hearlink)
    target_link_libraries(${t} pico_stdlib pico_multicore hardware_pio hardware_dma hardware_uart hardware_watchdog
                          hardware_clocks hardware_pwm pico_spdif_rx tinyusb_device)
    target_compile_definitions(${t} PRIVATE PICO_SPDIF_RX_PIO=0 PICO_SPDIF_RX_DMA_IRQ=0
                               PICO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK=1 PICO_CORE1_STACK_SIZE=0x1000
                               PICO_PANIC_FUNCTION=bridge_panic PICO_PANIC_FUNCTION_DOES_NOT_RETURN=1
                               PICO_STDIO_USB_ENABLE_TINYUSB_INIT=1 PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=1
                               PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0 PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
                               PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=0
                               SYS_CLK_HZ=200000000)   # 200 МГц — з SDK 2.1 офіційно для RP2040 (напруга 1,15 В — сама)
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    pico_enable_stdio_uart(${t} 0)
    pico_enable_stdio_usb(${t} 1)   # стан і команди на столі; через нього ж picotool переводить Pico в режим запису
endfunction()
add_executable(bridge ${BRIDGE_SRC})
bridge_setup(bridge)
set_source_files_properties(src/asrc.c PROPERTIES COMPILE_OPTIONS "-O2")
pico_add_linker_script_override_path(bridge ${CMAKE_CURRENT_LIST_DIR}/ld/app FILES pico_flash_region.ld section_boot2.incl)
pico_add_extra_outputs(bridge)

# ---- пробна збірка програми лише в ОЗУ (ninja bridge_ram; picotool load -x -f build/bridge_ram.uf2): флеш моста не
# змінюється, перезапуск — і знову програма з флешу. BRIDGE_RAM_CRC — CRC-32 програми у флеші моста (вона в стані
# передавачу; інакше він вирішить, що міст «не той», і перепише його): cmake … -DBRIDGE_RAM_CRC=0xafb343b3
set(BRIDGE_RAM_CRC 0 CACHE STRING "CRC-32 програми у флеші моста — для пробної збірки в ОЗУ")
add_executable(bridge_ram EXCLUDE_FROM_ALL ${BRIDGE_SRC})
bridge_setup(bridge_ram)
pico_set_binary_type(bridge_ram no_flash)
target_compile_definitions(bridge_ram PRIVATE BRIDGE_RAM_CRC=${BRIDGE_RAM_CRC})
pico_add_extra_outputs(bridge_ram)

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
