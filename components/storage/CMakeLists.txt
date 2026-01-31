idf_component_register(
    SRCS "src/nvs_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash ui
)
