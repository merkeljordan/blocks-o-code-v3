idf_component_register(
    SRCS 
        "main.c"
        "i2c_comm.c"
        "demo_task.c"
        "device_registry.c"
        "app.c"
        "block_config_manager.c"
    INCLUDE_DIRS "." 
    "../../include"
    REQUIRES json driver esp_event esp_wifi esp_netif esp_timer nvs_flash
)