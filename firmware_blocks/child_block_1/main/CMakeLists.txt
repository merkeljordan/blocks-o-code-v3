idf_component_register(
    SRCS 
        "main.c"
        "i2c_comm.c"
        "led_matrix.c"
        "command_handler.c"
    INCLUDE_DIRS "." "../.."
    REQUIRES driver led_strip
)