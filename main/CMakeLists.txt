cmake_minimum_required(VERSION 3.16)

# main 组件 CMake 配置，自动收集所有 C 源文件
file(GLOB_RECURSE MAIN_SOURCES "*.c")

idf_component_register(
    SRCS
        ${MAIN_SOURCES}
    INCLUDE_DIRS
        "."
    REQUIRES
        freertos
        driver
        u8g2
        u8g2-hal-esp-idf
        astraui_lite
)
