idf_component_register(
    SRCS "battery_monitor.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_adc esp_common log
)

# Most block projects keep battery_calibration.h in their main/ component.
if(EXISTS "${PROJECT_DIR}/main/battery_calibration.h")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${PROJECT_DIR}/main")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BATTERY_MONITOR_HAS_CAL_HEADER=1)
endif()
