idf_component_register(
    SRCS
        "speaker.cpp"
        "WAVFileReader.cpp"
        "DACOutput.cpp"
        "SinWaveGenerator.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver freertos
)