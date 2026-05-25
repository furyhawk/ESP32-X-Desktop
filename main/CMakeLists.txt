idf_component_register(
    SRCS "app/app_main.cpp"
         "modules/desktop_ui/desktop_ui.cpp"
         "modules/wifi_provisioning/wifi_provisioning.cpp"
    REQUIRES
         audio_bsp
         app_bsp
         pmicpower
         nvs_flash
         esp_http_client
         espressif__cjson
         mbedtls
    INCLUDE_DIRS "."
                 "app"
                 "modules/desktop_ui"
                 "modules/wifi_provisioning")
