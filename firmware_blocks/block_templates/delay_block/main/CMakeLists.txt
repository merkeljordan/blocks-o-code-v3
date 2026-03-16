idf_component_register(
    SRCS
        "main.c"
        "i2c_comm.c"
        "command_handler.c"
        "led_matrix.c"
        "speaker.c"
        "../../../shared_components/status_strip/status_strip.c"
    INCLUDE_DIRS "." "../.." "../../../include" "../../../shared_components/status_strip"
    REQUIRES driver led_strip
)
