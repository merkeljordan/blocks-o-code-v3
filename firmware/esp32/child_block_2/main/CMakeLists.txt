idf_component_register(
    SRCS 
        "main.c"
        "i2c_comm.c"
        "oled_display.c"
        "command_handler.c"
    INCLUDE_DIRS "." "../.."
    REQUIRES ssd1306 driver
)