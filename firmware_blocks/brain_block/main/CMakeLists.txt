idf_component_register(
    SRCS 
        "main.c"
        "i2c_comm.c"
        "device_registry.c"
        "app.c"
        "block_config_manager.c"
        "brain_event_handler.c"
        "esp32_lcd_display.c"
        "lcd_gui.c"
    INCLUDE_DIRS "." 
    "../../include"
    REQUIRES json driver esp_event esp_wifi esp_netif esp_timer nvs_flash lvgl lvgl_esp32_drivers
)