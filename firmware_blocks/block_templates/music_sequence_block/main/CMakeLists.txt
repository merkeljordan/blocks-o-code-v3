idf_component_register(
    SRCS
        "main.c"
        "i2c_comm.c"
        "tft_ui.c"
        "../speaker.c"
        "../music_sequence_bg.c"
    INCLUDE_DIRS "." ".." "../.." "../../../include"
    REQUIRES driver lvgl lvgl_esp32_drivers esp_timer
)
