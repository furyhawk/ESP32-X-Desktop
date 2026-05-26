idf_component_register(
    SRCS 
    "i2c_bsp.cpp"
    "display_bsp.cpp"
    PRIV_REQUIRES 
    driver
    esp_driver_i2c
    esp_lcd
    esp_lcd_sh8601
    esp_lcd_touch_cst9217
    pmicpower
    INCLUDE_DIRS ".")
