# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# ESP-IDF v6 moved i2c_master.h to esp_driver_i2c; expose it for managed components
if(EXISTS "$ENV{IDF_PATH}/components/esp_driver_i2c/include/driver/i2c_master.h")
    idf_build_set_property(INCLUDE_DIRECTORIES "$ENV{IDF_PATH}/components/esp_driver_i2c/include" APPEND)
endif()
project(01_AXP2101_Test)
