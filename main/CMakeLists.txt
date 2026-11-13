idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES xiaomi_lywsd03 platform_ble nvs_flash freertos log
)
