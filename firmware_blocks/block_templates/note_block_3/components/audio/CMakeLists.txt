# NOTE:
# The note block's active firmware path uses main/speaker.c and does not link
# against this legacy Arduino-based audio component. Keep this component
# discoverable but empty so ESP-IDF does not try to resolve Arduino/esp-dsp
# dependencies during note_block builds.
idf_component_register(
    INCLUDE_DIRS "."
)
