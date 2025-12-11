idf_component_register(
    SRCS 
        "Source_code/astra_ui_core.c"
        "Source_code/astra_ui_draw_driver.c" 
        "Source_code/astra_ui_drawer.c"
        "Source_code/astra_ui_item.c"
    INCLUDE_DIRS
        "Source_code"
    REQUIRES
        driver
        freertos
        u8g2

)