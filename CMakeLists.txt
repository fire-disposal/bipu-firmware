cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/components/drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/components/services/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/components/services/ble
    ${CMAKE_CURRENT_SOURCE_DIR}/components/services/message
    ${CMAKE_CURRENT_SOURCE_DIR}/components/services/effect
    ${CMAKE_CURRENT_SOURCE_DIR}/components/services/input
    ${CMAKE_CURRENT_SOURCE_DIR}/components/app
    ${CMAKE_CURRENT_SOURCE_DIR}/components/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/components/u8g2
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(bp_pager)