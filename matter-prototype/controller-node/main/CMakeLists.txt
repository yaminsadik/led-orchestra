# The OTA provider integration is compiled in but its body is gated by
# CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER (no-op stubs when disabled), so the
# default proven build is unchanged. The esp_matter_ota_provider dependency is
# only required when the option is enabled.
set(lo_priv_requires "")
if(CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER)
    list(APPEND lo_priv_requires esp_matter_ota_provider)
endif()

idf_component_register(SRCS
                           "app_main.cpp"
                           "controller_wifi_ingress.cpp"
                           "led_orchestra_console.cpp"
                           "led_orchestra_ota_provider.cpp"
                       INCLUDE_DIRS
                           "."
                           "../../common"
                       REQUIRES
                           esp_event
                           esp_netif
                           esp_wifi
                       PRIV_REQUIRES
                           "${lo_priv_requires}"
                       LDFRAGMENTS
                           "linker.lf")

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
