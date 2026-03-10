idf_component_register(
    SRCS
        "main.c"
        "i2c_comm.c"
        "command_handler.c"
        "led_matrix.c"
    INCLUDE_DIRS "." "../.." "../../../include"
    REQUIRES driver led_strip audio espressif__arduino-esp32
)
