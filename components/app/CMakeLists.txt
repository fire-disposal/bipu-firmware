idf_component_register(
    SRCS "src/app.c"
         "src/app_battery.c"
         "src/app_ble.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ble board ui
)
