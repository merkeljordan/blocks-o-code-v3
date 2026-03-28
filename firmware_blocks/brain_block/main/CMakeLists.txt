idf_component_register(
    SRCS 
        "main.c"
        "i2c_comm.c"
        "device_registry.c"
        "app.c"
        "block_config_manager.c"
        "brain_event_handler.c"
        "esp32_lcd_display_v9.c"
        "../../shared_components/status_strip/status_strip.c"
    INCLUDE_DIRS "." 
        "../../include"
        "../../shared_components/status_strip"
    REQUIRES json driver esp_event esp_wifi esp_netif esp_timer nvs_flash lvgl esp_lcd esp_lcd_ili9341 esp_lcd_touch_xpt2046 espressif__arduino-esp32 audio led_strip battery_monitor
)
