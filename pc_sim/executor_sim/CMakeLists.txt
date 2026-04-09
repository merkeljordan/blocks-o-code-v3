cmake_minimum_required(VERSION 3.16)

project(executor_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(executor_sim
    main.c
    mock_esp_framework.c
    mock_i2c.c
    mock_components.c
    ../../firmware_blocks/brain_block/main/brain_event_handler.c
)

target_include_directories(executor_sim PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/brain_block/main
    ${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/include
    ${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/shared_components/status_strip
    ${CMAKE_CURRENT_LIST_DIR}/../../firmware_blocks/shared_components/audio_speaker
)
