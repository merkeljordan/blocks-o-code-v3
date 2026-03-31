cmake_minimum_required(VERSION 3.10)

set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "" FORCE)

set(EXTRA_COMPONENT_DIRS
    "components"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared_components/battery_monitor"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(led_color_flash_block)
