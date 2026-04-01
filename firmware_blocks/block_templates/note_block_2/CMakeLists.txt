cmake_minimum_required(VERSION 3.5)

# Reuse shared audio + peripherals.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../shared_components/audio"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared_components/battery_monitor"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared_components/startup_guard"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(note_block)
