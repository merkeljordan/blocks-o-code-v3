idf_component_register(
    SRCS
        "main.c"
        "tft_ui.c"
        "../speaker.c"
    INCLUDE_DIRS "." ".." "../.." "../../../include"
    REQUIRES driver lvgl lvgl_esp32_drivers esp_timer
)
