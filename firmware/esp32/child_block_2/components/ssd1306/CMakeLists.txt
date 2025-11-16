if(CONFIG_SPI_INTERFACE)
    # SPI only - exclude I2C files completely
    idf_component_register(
        SRCS 
            "ssd1306.c" 
            "ssd1306_spi.c"
        INCLUDE_DIRS "."
        REQUIRES driver
    )
else()
    # I2C only (when CONFIG_I2C_INTERFACE is selected)
    idf_component_register(
        SRCS 
            "ssd1306.c" 
            "ssd1306_i2c_new.c"
        INCLUDE_DIRS "."
        REQUIRES driver
    )
endif()