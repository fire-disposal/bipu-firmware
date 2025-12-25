cmake_minimum_required(VERSION 3.16)

# 只需要包含u8g2组件
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/components/u8g2
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(bp_pager)