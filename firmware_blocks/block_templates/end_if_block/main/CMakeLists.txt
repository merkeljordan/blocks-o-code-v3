idf_component_register(
    SRCS
        "main.c"
        "i2c_comm.c"
        "command_handler.c"
        "led_matrix.c"
        "tft_ui.c"
        "../../../shared/control_flow_tft_ui/control_flow_tft_ui.c"
        "../../../shared/control_flow_tft_ui/control_flow_tft_hw.c"
        "../../../shared_components/status_strip/status_strip.c"
    INCLUDE_DIRS "." "../.." "../../../include" "../../../shared/control_flow_tft_ui" "../../../shared_components/status_strip"
    REQUIRES
        driver
        led_strip
        audio
        espressif__arduino-esp32
        lvgl
        esp_lcd
        espressif__esp_lcd_ili9341
        atanisoft__esp_lcd_touch_xpt2046
        esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE CONTROL_FLOW_TFT_UI_ENABLED=1)
