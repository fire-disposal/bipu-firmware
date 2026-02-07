idf_component_register(
    SRCS "ble_manager.c"
         "src/ble_protocol.c"
         "src/ble_bipupu_service.c"
         "src/ble_battery_service.c"
         "src/ble_cts_service.c"
    INCLUDE_DIRS "include"
    REQUIRES bt storage
    PRIV_REQUIRES nvs_flash
)