idf_component_register(
    SRCS 
    "i2c_bsp.cpp" 
    PRIV_REQUIRES 
    driver
    esp_driver_i2c
    INCLUDE_DIRS ".")
