cmake_minimum_required(VERSION 3.16)

# Make shared components (like battery_monitor) visible to this project
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../shared_components
)

# Include the ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define your project name
project(brain_block)
