idf_component_register(
    SRCS "src/app.c"
         "src/app_conn_sm.c"
         "src/app_battery.c"
         "src/app_effects.c"
         "src/app_ble.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ble board ui
)
