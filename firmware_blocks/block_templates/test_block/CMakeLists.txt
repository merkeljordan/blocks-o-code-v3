cmake_minimum_required(VERSION 3.10)

# Help embedded toolchains avoid command-line length limits.
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "" FORCE)

# Single-component path: if the directory has CMakeLists.txt, IDF registers it as that
# component (see __project_component_dir in esp-idf/tools/cmake/project.cmake). Do not
# point at shared_components/ alone or every subfolder (audio, led_matrix, …) is built.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../shared_components/battery_monitor"
    "${CMAKE_CURRENT_LIST_DIR}/../../shared_components/startup_guard"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_block)

