cmake_minimum_required(VERSION 3.10)

# Some embedded toolchains hit command-line length limits; force response files.
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "" FORCE)

# Local components (including ./components/audio) live here.
set(EXTRA_COMPONENT_DIRS
    "components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(music_sequence_block)
