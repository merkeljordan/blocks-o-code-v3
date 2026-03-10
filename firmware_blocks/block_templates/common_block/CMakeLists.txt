cmake_minimum_required(VERSION 3.10)

set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "" FORCE)

set(EXTRA_COMPONENT_DIRS "components")

# Make shared components (like battery_monitor) visible to this template
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../shared
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(block_template)
