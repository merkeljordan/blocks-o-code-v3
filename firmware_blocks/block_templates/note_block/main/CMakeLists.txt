idf_component_register(
    SRCS
        "main.c"
        "i2c_comm.c"
        "led_matrix.c"
        "speaker.c"
        "tft_ui.c"
        "../../../shared_components/status_strip/status_strip.c"
    INCLUDE_DIRS "." "../.." "../../../include" "../../../shared_components/status_strip"
    REQUIRES driver led_strip lvgl esp_lcd espressif__esp_lcd_ili9341 atanisoft__esp_lcd_touch_xpt2046 esp_timer espressif__arduino-esp32 audio battery_monitor startup_guard
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LED_MATRIX_HAS_STATUS_STRIP)
