cmake_minimum_required(VERSION 3.16)

# Reuse the locally supplied SH8601 driver and ESP-IDF component utilities.
# This keeps the production project self-contained under D:\my_project\Pacon
# and compatible with the user's ESP-IDF 5.4.3 VS Code setup.
set(PACON_CMAKE_UTILITIES
    "${CMAKE_CURRENT_LIST_DIR}/../Pacon/components/espressif__cmake_utilities")
list(APPEND CMAKE_MODULE_PATH "${PACON_CMAKE_UTILITIES}")
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../Pacon/managed_components/espressif__cmake_utilities"
    "${CMAKE_CURRENT_LIST_DIR}/../Pacon/components/esp_lcd_sh8601"
    "${CMAKE_CURRENT_LIST_DIR}/../Pacon/components/lvgl__lvgl"
    "${CMAKE_CURRENT_LIST_DIR}/../../smartknob/superdial_port/references/superdial-upstream/idf_code/components/espressif__esp_tinyusb"
    "${CMAKE_CURRENT_LIST_DIR}/../../smartknob/superdial_port/references/superdial-upstream/idf_code/components/espressif__tinyusb"
    "${CMAKE_CURRENT_LIST_DIR}/../../smartknob/superdial_port/references/superdial-upstream/idf_code/components/espressif__esp_tinyusb"
    "${CMAKE_CURRENT_LIST_DIR}/../../smartknob/superdial_port/references/superdial-upstream/idf_code/components/espressif__tinyusb")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pacon_fluid_pendant)
