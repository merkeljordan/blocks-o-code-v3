cmake_minimum_required(VERSION 3.5)

# Reuse shared components from the brain_block (LED matrix, speaker, etc.)
set(EXTRA_COMPONENT_DIRS
    "../../brain_block/components"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared_components/battery_monitor"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared_components/startup_guard"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(note_block)
