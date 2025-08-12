# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS components/usb/usb_stream)

string(TIMESTAMP COMPILE_TIME %Y%m%d)        # 生成编译时间
set(PROJECT_VER "NE_101.1.0.2")             # NE_101.2.x.x CE 版本 NE_101.1.x.x FCC 版本
project(NE101_${COMPILE_TIME})            # 固件名称
