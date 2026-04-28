idf_component_register(
    SRCS
        "main.c"
        "i2c_comm.c"
        "music_leds.c"
        "tft_ui.c"
        "speaker_music.c"
        "../../../shared_components/led_matrix/led_matrix.c"
        "../../../shared_components/status_strip/status_strip.c"
    INCLUDE_DIRS
        "."
        ".."
        "../.."
        "../../../include"
        "../../../shared_components/led_matrix"
        "../../../shared_components/status_strip"
    REQUIRES
        driver
        esp_lcd
        esp_timer
        lvgl
        espressif__arduino-esp32
        espressif__esp_lcd_ili9341
        atanisoft__esp_lcd_touch_xpt2046
        audio
        battery_monitor
        startup_guard
        led_matrix
    # Songs are embedded in flash and exposed as linker symbols.
    # The 4 MB hardware target embeds the final 5-song catalog here.
    EMBED_FILES
       "audio/super_mario.wav"
       "audio/hakuna_matata.wav"
       "audio/sunflower.wav"
       "audio/see_you_again.wav"
       "audio/long_train_runnin.wav"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LED_MATRIX_HAS_STATUS_STRIP)
