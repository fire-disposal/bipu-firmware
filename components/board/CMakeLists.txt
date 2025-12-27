idf_component_register(
    SRCS "board.c"
    INCLUDE_DIRS "include"
    REQUIRES driver u8g2 esp_timer
)
