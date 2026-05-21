cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Work around managed-component warnings that are promoted by global -Werror.
add_compile_options(-Wno-error=attributes -Wno-error=cpp -Wno-error=missing-field-initializers)

project(09_LVGL_V9_Test)
