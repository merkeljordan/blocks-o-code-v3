idf_component_register(
    SRCS
        "main.c"
        "i2c_comm.c"
        "led_matrix.c"
    INCLUDE_DIRS "." "../.." "../../../include"
    REQUIRES driver led_strip audio led_ux espressif__arduino-esp32
)
