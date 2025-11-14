idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "../.."  # Add parent-parent directory
    REQUIRES driver led_strip
) 