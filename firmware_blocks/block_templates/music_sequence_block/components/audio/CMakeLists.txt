idf_component_register(
    SRCS
        "speaker.cpp"
        "DACOutput.cpp"
        "WAVFileReader.cpp"
        "SinWaveGenerator.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        espressif__arduino-esp32
    # Boot audio clip embedded into this component.
    EMBED_FILES
        "audio/bootupsound.wav"
)
