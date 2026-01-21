idf_component_register(
    SRCS 
        "main.c"
        "i2c_comm.c"
        "demo_task.c"
        "device_registry.c"
        "app.c"
    INCLUDE_DIRS "."
    "../../include"
)