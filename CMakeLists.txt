cmake_minimum_required(VERSION 3.16)

# 包含自定义组件目录
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/components
)

# 启用BLE功能
set(CONFIG_BT_ENABLED ON)
set(CONFIG_BT_BLE_ENABLED ON)
set(CONFIG_BT_GATTS_ENABLE ON)
set(CONFIG_BT_NIMBLE_ENABLED ON)
set(CONFIG_BT_BLUEDROID_ENABLED OFF)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(bp_pager)