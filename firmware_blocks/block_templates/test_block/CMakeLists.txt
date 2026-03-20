cmake_minimum_required(VERSION 3.10)

# Help embedded toolchains avoid command-line length limits.
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "" FORCE)

# No extra components needed for this bring-up app.

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_block)

