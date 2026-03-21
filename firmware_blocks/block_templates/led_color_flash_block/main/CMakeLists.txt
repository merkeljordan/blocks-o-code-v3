idf_component_register(
    SRCS
        "main.c"
        "i2c_comm.c"
        "command_handler.c"
        "led_matrix.c"
        "tft_ui.c"
    INCLUDE_DIRS "." "../.." "../../../include"
    REQUIRES driver led_strip lvgl esp_lcd espressif__esp_lcd_ili9341 atanisoft__esp_lcd_touch_xpt2046 esp_timer audio espressif__arduino-esp32 battery_monitor
)
