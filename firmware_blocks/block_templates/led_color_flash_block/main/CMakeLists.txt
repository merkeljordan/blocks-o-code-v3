idf_component_register(
    SRCS
        "main.c"
        "i2c_comm.c"
        "command_handler.c"
        "led_matrix.c"
        "speaker.c"
        "tft_ui.c"
        "../illustration-of-cheerful-kids-dancing-together-png.c"
    INCLUDE_DIRS "." "../.." "../../../include"
    REQUIRES driver led_strip lvgl lvgl_esp32_drivers esp_timer
)
