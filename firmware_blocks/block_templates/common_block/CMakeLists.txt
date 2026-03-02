cmake_minimum_required(VERSION 3.5)

# Make shared components (like battery_monitor) visible to this template
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../shared
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(block_template)
