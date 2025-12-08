# The name of your project
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/components/u8g2
    ${CMAKE_CURRENT_SOURCE_DIR}/components/u8g2-hal-esp-idf
    ${CMAKE_CURRENT_SOURCE_DIR}/components/astraui_lite
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The name of your project executable
project(bipupu)