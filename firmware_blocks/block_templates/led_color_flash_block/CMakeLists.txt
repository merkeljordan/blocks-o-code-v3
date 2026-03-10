cmake_minimum_required(VERSION 3.10)

set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "" FORCE)

# Include local components and shared components (like battery_monitor)
set(EXTRA_COMPONENT_DIRS
    "components"
    ${CMAKE_CURRENT_LIST_DIR}/../../shared
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(led_color_flash_block)
