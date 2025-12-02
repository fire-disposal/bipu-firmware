# The name of your project
cmake_minimum_required(VERSION 3.16)

# 关键修正：告诉构建系统去哪里找自定义组件
# 将项目根目录下的 'custom_component' 文件夹添加到搜索路径
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/paermkn)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The name of your project executable
project(my_test_app)