idf_component_register(
    SRCS "led_matrix.c"
    INCLUDE_DIRS "."
    REQUIRES led_strip freertos driver
)

