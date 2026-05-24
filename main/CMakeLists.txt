idf_component_register(
    SRCS "app/app_main.cpp"
         "modules/desktop_ui/desktop_ui.cpp"
         "modules/wifi_provisioning/wifi_provisioning.cpp"
    INCLUDE_DIRS "."
                 "app"
                 "modules/desktop_ui"
                 "modules/wifi_provisioning")
