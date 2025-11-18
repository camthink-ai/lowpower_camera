# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS components/usb/usb_stream)

string(TIMESTAMP COMPILE_TIME %Y%m%d)       # 生成编译时间
set(PROJECT_VER "NE101_V1.5_FCC_PIR")         
project(NE101_${COMPILE_TIME})              # 固件名称
