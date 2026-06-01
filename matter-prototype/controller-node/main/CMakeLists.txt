idf_component_register(SRCS
                           "app_main.cpp"
                           "controller_wifi_ingress.cpp"
                           "led_orchestra_console.cpp"
                       INCLUDE_DIRS
                           "."
                           "../../common"
                       REQUIRES
                           esp_event
                           esp_netif
                           esp_wifi
                       LDFRAGMENTS
                           "linker.lf")

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
