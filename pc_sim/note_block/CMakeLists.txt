cmake_minimum_required(VERSION 3.16)

project(note_block_sim C)

# Point this at the LVGL source already vendored in this repo (managed component).
set(LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/brain_block/managed_components/lvgl__lvgl")

# Provide a simulator-specific LVGL config (enables SDL driver).
set(LV_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

add_subdirectory("${LVGL_DIR}" lvgl_build)

# LVGL's SDL driver sources are compiled into the lvgl target, so lvgl itself
# needs SDL2 include directories (provided by the SDL2 imported target).
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

add_executable(note_block_sim
    main.c
    "${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/block_templates/note_block/main/tft_ui.c"
)

target_compile_definitions(note_block_sim PRIVATE NOTE_UI_SIMULATOR=1)

target_include_directories(note_block_sim PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/block_templates/note_block/main"
)

# LVGL's built-in SDL driver needs SDL2 headers available to LVGL too.
target_link_libraries(note_block_sim PRIVATE
    lvgl
    SDL2::SDL2
    SDL2::SDL2main
)

