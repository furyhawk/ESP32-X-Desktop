cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Reuse the proven codec_board component for startup audio chime support.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/02_Example/07_Audio_Test/components/ExternLib/codec_board")

# ESP-IDF v6 moved i2c_master.h to esp_driver_i2c; expose it for managed components
if(EXISTS "$ENV{IDF_PATH}/components/esp_driver_i2c/include/driver/i2c_master.h")
    idf_build_set_property(INCLUDE_DIRECTORIES "$ENV{IDF_PATH}/components/esp_driver_i2c/include" APPEND)
endif()

# ESP-IDF v6 splits more driver headers; keep legacy include resolution for codec stack.
if(EXISTS "$ENV{IDF_PATH}/components/esp_driver_gpio/include/driver/gpio.h")
    idf_build_set_property(INCLUDE_DIRECTORIES "$ENV{IDF_PATH}/components/esp_driver_gpio/include" APPEND)
endif()
if(EXISTS "$ENV{IDF_PATH}/components/esp_driver_i2s/include/driver/i2s_std.h")
    idf_build_set_property(INCLUDE_DIRECTORIES "$ENV{IDF_PATH}/components/esp_driver_i2s/include" APPEND)
endif()
if(EXISTS "$ENV{IDF_PATH}/components/esp_driver_spi/include/driver/spi_common.h")
    idf_build_set_property(INCLUDE_DIRECTORIES "$ENV{IDF_PATH}/components/esp_driver_spi/include" APPEND)
endif()
if(EXISTS "$ENV{IDF_PATH}/components/esp_hal_gpspi/include/hal/spi_types.h")
    idf_build_set_property(INCLUDE_DIRECTORIES "$ENV{IDF_PATH}/components/esp_hal_gpspi/include" APPEND)
endif()
if(EXISTS "$ENV{IDF_PATH}/components/esp_hal_i2s/include/hal/i2s_types.h")
    idf_build_set_property(INCLUDE_DIRECTORIES "$ENV{IDF_PATH}/components/esp_hal_i2s/include" APPEND)
endif()

# Work around managed-component warnings that are promoted by global -Werror.
add_compile_options(-Wno-error=attributes -Wno-error=cpp -Wno-error=missing-field-initializers)

project(LVGL_ESP32_DESKTOP_UI)
