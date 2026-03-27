idf_component_register(
    SRCS
        "main.c"
        "i2c_comm.c"
        "led_matrix.c"
        "../../../shared_components/status_strip/status_strip.c"
    INCLUDE_DIRS "." "../.." "../../../include" "../../../shared_components/status_strip"
    REQUIRES driver led_strip audio espressif__arduino-esp32
)
