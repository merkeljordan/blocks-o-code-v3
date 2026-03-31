cmake_minimum_required(VERSION 3.10)

# Help embedded toolchains avoid command-line length limits.
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "" FORCE)

# Local components (including ./components/audio) live here.
set(EXTRA_COMPONENT_DIRS
    "components"
<<<<<<< HEAD
    "${CMAKE_CURRENT_LIST_DIR}/../../shared_components/battery_monitor"
=======
>>>>>>> origin/main
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(if_block)
