idf_component_register(
    SRCS
        "ble_manager.c"
        "src/bipupu_protocol.c"
        # 移除旧的协议文件
        # "src/ble_protocol.c"
        # "src/ble_nus_service.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        bt
        storage
        esp_timer
    PRIV_REQUIRES
        nvs_flash
)
