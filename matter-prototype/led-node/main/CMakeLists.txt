idf_component_register(SRCS
                           "app_main.cpp"
                           "led_orchestra_cluster.cpp"
                           "led_orchestra_renderer.cpp"
                       INCLUDE_DIRS
                           "."
                           "../../common")

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")

idf_component_get_property(esp_matter_lib espressif__esp_matter COMPONENT_LIB)
if(esp_matter_lib)
    set(closure_control_logic
        "${PROJECT_DIR}/managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/app/clusters/closure-control-server/closure-control-cluster-logic.cpp")
    get_target_property(esp_matter_sources ${esp_matter_lib} SOURCES)
    if(esp_matter_sources)
        list(REMOVE_ITEM esp_matter_sources "${closure_control_logic}")
        set_target_properties(${esp_matter_lib} PROPERTIES SOURCES "${esp_matter_sources}")
    endif()
endif()
