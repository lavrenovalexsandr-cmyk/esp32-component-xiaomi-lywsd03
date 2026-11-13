cmake_minimum_required(VERSION 3.16)

# esp32-platform-ble is maintained as a separate repository.
# By default, expect both repositories to be checked out side by side.
# Override with: -DESP32_PLATFORM_BLE_PATH=/path/to/esp32-platform-ble
set(
    ESP32_PLATFORM_BLE_PATH
    "${CMAKE_CURRENT_LIST_DIR}/../esp32-platform-ble"
    CACHE PATH
    "Path to the esp32-platform-ble repository"
)

if(NOT EXISTS "${ESP32_PLATFORM_BLE_PATH}/components/platform_ble/CMakeLists.txt")
    message(FATAL_ERROR
        "esp32-platform-ble component not found. Clone "
        "https://github.com/lavrenovalexsandr-cmyk/esp32-platform-ble "
        "next to this repository or set ESP32_PLATFORM_BLE_PATH."
    )
endif()

set(EXTRA_COMPONENT_DIRS
    "${ESP32_PLATFORM_BLE_PATH}/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_xiaomi_lywsd03)
