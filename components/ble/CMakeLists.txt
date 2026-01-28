idf_component_register(
    SRCS "ble_manager.c" "src/ble_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash
)
