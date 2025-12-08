# 声明 main 组件的源文件
idf_component_register(SRCS "main.cpp" "astra_ui_example.cpp"
                       REQUIRES freertos driver u8g2 u8g2-hal-esp-idf astraui_lite)