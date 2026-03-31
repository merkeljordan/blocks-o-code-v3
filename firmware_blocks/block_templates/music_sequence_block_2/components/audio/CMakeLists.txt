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
)
