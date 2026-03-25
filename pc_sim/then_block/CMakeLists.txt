cmake_minimum_required(VERSION 3.16)

project(then_block_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/brain_block/managed_components/lvgl__lvgl")
set(LV_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h")

find_package(SDL2 REQUIRED)

add_subdirectory("${LVGL_DIR}" lvgl_build)
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

add_executable(then_block_sim
    main.c
    "${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/block_templates/then_block/main/tft_ui.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/shared/control_flow_tft_ui/control_flow_tft_ui.c"
)

target_include_directories(then_block_sim PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/block_templates/then_block/main"
    "${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/shared/control_flow_tft_ui"
)

target_link_libraries(then_block_sim PRIVATE
    lvgl
    SDL2::SDL2
)

if(TARGET SDL2::SDL2main)
    target_link_libraries(then_block_sim PRIVATE SDL2::SDL2main)
endif()
