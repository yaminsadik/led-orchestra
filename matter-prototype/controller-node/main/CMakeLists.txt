idf_component_register(SRCS
                           "app_main.cpp"
                           "led_orchestra_console.cpp"
                       INCLUDE_DIRS
                           "."
                           "../../common"
                       LDFRAGMENTS
                           "linker.lf")

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
