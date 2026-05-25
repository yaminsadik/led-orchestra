idf_component_register(SRCS
                           "app_main.cpp"
                           "led_orchestra_cluster.cpp"
                           "led_orchestra_renderer.cpp"
                       INCLUDE_DIRS
                           "."
                           "../../common")

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
