idf_component_register(
    SRCS "ui_display.c" "ui_content.c" "ui_input.c" "ui_custom.c"
    INCLUDE_DIRS "include"
    REQUIRES driver u8g2 astraui_lite
)