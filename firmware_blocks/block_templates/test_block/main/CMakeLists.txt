idf_component_register(
    SRCS
        "main.c"
        "led_dual_ws2812.c"
        "tft_test_ui.c"
        "speaker.c"
    INCLUDE_DIRS
        "."
        ".."
        "../../../include"
        
    REQUIRES
        driver
        led_strip
        lvgl
        esp_lcd
        esp_timer
        espressif__esp_lcd_ili9341
        atanisoft__esp_lcd_touch_xpt2046
        battery_monitor
        startup_guard
)

